#include "clientcore/control/QcControlWorldCommands.h"

#include "clientcore/control/QcControlAim.h"
#include "clientcore/control/QcControlArguments.h"
#include "clientcore/control/QcControlCommandTable.h"
#include "clientcore/control/QcControlContext.h"
#include "clientcore/session/QcClientSession.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/world/QcLightVolume.h"

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

// The block at `pos`; none for air or a block that is not loaded.
[[nodiscard]] std::optional<QJsonObject> blockJson(const QcClientWorld &world, QcBlockPos pos)
{
    const QcContentRegistry &content = world.content();
    const std::optional<QcBlockId> id = world.blockAt(pos);
    if (!id || *id == qcAirBlockId) {
        return std::nullopt;
    }

    QJsonObject entry{{u"x"_s, pos.x}, {u"y"_s, pos.y}, {u"z"_s, pos.z}, {u"block"_s, content.block(*id).name}};
    if (!content.block(*id).properties.isEmpty()) {
        entry.insert(u"state"_s, stateJson(content, *id));
    }
    return entry;
}

// Every non-air block in the cube of `radius` around `centre`.
QJsonArray blocksAround(const QcClientWorld &world, QcBlockPos centre, int radius)
{
    QJsonArray blocks;
    // The cells of the cube in the order x, y, z (z fastest).
    const int side = 2 * radius + 1;
    for (int cell = 0; cell < side * side * side; ++cell) {
        const QcBlockPos pos = centre.offset(cell / (side * side) - radius, cell / side % side - radius, cell % side - radius);
        if (const std::optional<QJsonObject> block = blockJson(world, pos)) {
            blocks.append(*block);
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
    table.add(u"shoot"_s, [this](const QJsonObject &request) { shoot(request); });
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
    } else if (session().hand().target()) {
        pos = session().hand().target()->block;
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

void QcControlWorldCommands::shoot(const QJsonObject &request)
{
    const std::shared_ptr<const QcContentRegistry> content = session().content();
    int bow = -1;
    for (int slot = 0; content && slot < QcInventory::hotbarSize && bow < 0; ++slot) {
        const QcItemStack &stack = session().inventory().slot(slot);
        bow = !stack.isEmpty() && content->isValidItem(stack.item) && content->item(stack.item).launcher ? slot : -1;
    }
    if (bow < 0) {
        m_context.fail(request, u"No launcher (bow) in the hotbar"_s);
        return;
    }

    session().selectHotbarSlot(bow);
    QcPlayerInput intent = session().intent();
    intent.yaw = float(std::fmod(request.value(u"yaw"_s).toDouble(intent.yaw), 360.0));
    intent.pitch = float(std::clamp(request.value(u"pitch"_s).toDouble(intent.pitch), -90.0, 90.0));
    session().setIntent(intent);

    // One tick for the view to reach the server, the draw, then the release.
    const quint64 startTick = session().tickCount();
    const int ticks = QcControlArguments::ticks(request, 20);
    std::optional<quint64> drawnAt;
    m_context.begin({request, QcControlActiveCommand::timeoutPolls(ticks + 2), [this, startTick, ticks, drawnAt]() mutable {
                         if (!drawnAt) {
                             if (session().tickCount() > startTick) {
                                 session().useItem();
                                 drawnAt = session().tickCount();
                             }
                             return false;
                         }
                         if (session().tickCount() < *drawnAt + quint64(ticks)) {
                             return false;
                         }
                         session().releaseUse();
                         return true;
                     }});
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
