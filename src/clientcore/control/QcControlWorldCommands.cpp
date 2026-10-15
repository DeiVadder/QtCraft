#include "QcControlWorldCommands.h"

#include "QcClientSession.h"
#include "QcContentRegistry.h"
#include "QcControlAim.h"
#include "QcControlArguments.h"
#include "QcControlCommandTable.h"
#include "QcControlContext.h"
#include "QcLightVolume.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr int maxBlockQueryRadius = 8;

QJsonObject stateJson(const QcContentRegistry &content, QcBlockId id)
{
    QJsonObject state;
    for (const QcBlockProperty &property : content.block(id).properties) {
        state.insert(property.name, content.property(id, property.name));
    }
    return state;
}

// Appends the block at `pos` unless it is air or not loaded.
void appendBlock(QJsonArray &blocks, const QcClientWorld &world, QcBlockPos pos)
{
    const QcContentRegistry &content = world.content();
    const std::optional<QcBlockId> id = world.blockAt(pos);
    if (!id || *id == qcAirBlockId) {
        return;
    }

    QJsonObject entry{{u"x"_s, pos.x}, {u"y"_s, pos.y}, {u"z"_s, pos.z}, {u"block"_s, content.block(*id).name}};
    if (!content.block(*id).properties.isEmpty()) {
        entry.insert(u"state"_s, stateJson(content, *id));
    }
    blocks.append(entry);
}

// Every non-air block in the cube of `radius` around `centre`.
QJsonArray blocksAround(const QcClientWorld &world, QcBlockPos centre, int radius)
{
    QJsonArray blocks;
    for (int dx = -radius; dx <= radius; ++dx) {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dz = -radius; dz <= radius; ++dz) {
                appendBlock(blocks, world, centre.offset(dx, dy, dz));
            }
        }
    }
    return blocks;
}

} // namespace

QcControlWorldCommands::QcControlWorldCommands(QcControlContext &context)
    : m_context(context)
{
}

void QcControlWorldCommands::registerCommands(QcControlCommandTable &table)
{
    table.add(u"mine"_s, [this](const QJsonObject &request) { mine(request); });
    table.add(u"place"_s, [this](const QJsonObject &request) { useOnBlock(request); });
    table.add(u"use"_s, [this](const QJsonObject &request) { useOnBlock(request); });
    table.add(u"ignite"_s, [this](const QJsonObject &request) { ignite(request); });
    table.add(u"block"_s, [this](const QJsonObject &request) { block(request); });
    table.add(u"blocks"_s, [this](const QJsonObject &request) { blocks(request); });
}

QcClientSession &QcControlWorldCommands::session() const
{
    return *m_context.session();
}

void QcControlWorldCommands::block(const QJsonObject &request)
{
    const QcClientWorld *world = session().world();
    if (!world) {
        m_context.fail(request, u"World not loaded yet"_s);
        return;
    }

    const QcContentRegistry &content = world->content();
    const QcBlockPos pos = QcControlArguments::block(request);
    const std::optional<QcBlockId> id = world->blockAt(pos);

    QJsonObject answer{{u"block"_s, id ? QJsonValue(content.block(*id).name) : QJsonValue()}};
    if (id) {
        answer.insert(u"state"_s, stateJson(content, *id));
        answer.insert(u"blockLight"_s, QcLightVolume::blockLightAt(*world, pos));
    }
    m_context.finish(request, answer);
}

void QcControlWorldCommands::blocks(const QJsonObject &request)
{
    const QcClientWorld *world = session().world();
    if (!world) {
        m_context.fail(request, u"World not loaded yet"_s);
        return;
    }

    const int radius = std::clamp(QcControlArguments::intField(request, u"radius"_s, 3), 0, maxBlockQueryRadius);
    const QcVec3 p = session().playerMotion().position;
    const QcBlockPos centre{int(std::floor(p.x)), int(std::floor(p.y)), int(std::floor(p.z))};
    m_context.finish(request, {{u"centre"_s, QJsonArray{centre.x, centre.y, centre.z}}, {u"blocks"_s, blocksAround(*world, centre, radius)}});
}

void QcControlWorldCommands::mine(const QJsonObject &request)
{
    std::optional<QcBlockPos> pos;
    if (request.contains(u"x"_s)) {
        pos = QcControlArguments::block(request);
        QcControlAim::lookAt(session(), *pos, std::nullopt);
    } else if (session().target()) {
        pos = session().target()->block;
    }

    if (!pos || !session().world()) {
        m_context.fail(request, u"No block targeted"_s);
        return;
    }

    session().setMining(true);
    const int maxTicks = std::clamp(QcControlArguments::intField(request, u"maxTicks"_s, 400), 1, QcControlArguments::maxCommandTicks);
    m_context.begin({request, QcControlActiveCommand::timeoutPolls(maxTicks), [this, pos] {
                         const std::optional<QcBlockId> block = session().world()->blockAt(*pos);
                         if (block && *block != qcAirBlockId) {
                             return false;
                         }

                         session().setMining(false);
                         return true;
                     }});
}

void QcControlWorldCommands::ignite(const QJsonObject &request)
{
    const std::shared_ptr<const QcContentRegistry> content = session().content();
    const QcInventory &inventory = session().inventory();
    for (int slot = 0; content && slot < QcInventory::hotbarSize; ++slot) {
        const QcItemStack &stack = inventory.slot(slot);
        if (!stack.isEmpty() && content->isValidItem(stack.item) && content->item(stack.item).ignites) {
            session().selectHotbarSlot(slot);
            useOnBlock(request);
            return;
        }
    }
    m_context.fail(request, u"No item that lights fire (flint and steel) in the hotbar"_s);
}

void QcControlWorldCommands::useOnBlock(const QJsonObject &request)
{
    const quint64 startTick = session().tickCount();
    if (request.contains(u"x"_s)) {
        const std::optional<QcBlockFace> face = QcControlArguments::face(request);
        if (!face) {
            m_context.fail(request, u"'face' must be east, west, up, down, south or north"_s);
            return;
        }

        QcControlAim::lookAt(session(), QcControlArguments::block(request), face);
    }

    // Wait one tick so the session's target follows the new view direction, then use the item.
    bool used = false;
    m_context.begin({request, QcControlActiveCommand::timeoutPolls(4), [this, startTick, used]() mutable {
                         if (!used) {
                             if (session().tickCount() <= startTick) {
                                 return false;
                             }
                             if (!session().useItem()) {
                                 return true;
                             }
                             used = true;
                             return false;
                         }

                         return session().tickCount() >= startTick + 4;
                     }});
}
