#include "clientcore/control/QcControlInventoryCommands.h"

#include "clientcore/control/QcControlAim.h"
#include "clientcore/control/QcControlArguments.h"
#include "clientcore/control/QcControlCommandTable.h"
#include "clientcore/control/QcControlContext.h"
#include "clientcore/session/QcClientSession.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcCrafting.h"

#include <QJsonArray>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

// Ticks a command waits for the server to answer an inventory change.
constexpr int inventoryTicks = 3;

} // namespace

QcControlInventoryCommands::QcControlInventoryCommands(QcControlContext &context)
    : m_context(context)
{
}

void QcControlInventoryCommands::registerCommands(QcControlCommandTable &table)
{
    table.add(u"drop"_s, [this](const QJsonObject &request) { drop(request); });
    table.add(u"select"_s, [this](const QJsonObject &request) { select(request); });
    table.add(u"inventory"_s, [this](const QJsonObject &request) { inventory(request); });
    table.add(u"moveItem"_s, [this](const QJsonObject &request) { moveItem(request); });
    table.add(u"closeCrafting"_s, [this](const QJsonObject &request) { closeCrafting(request); });
    table.add(u"recipes"_s, [this](const QJsonObject &request) { recipes(request); });
    table.add(u"craft"_s, [this](const QJsonObject &request) { craft(request); });
    table.add(u"takeResult"_s, [this](const QJsonObject &request) { takeResult(request); });
    table.add(u"creativeItem"_s, [this](const QJsonObject &request) { creativeItem(request); });
    table.add(u"pickBlock"_s, [this](const QJsonObject &request) { pickBlock(request); });
}

QcClientSession &QcControlInventoryCommands::session() const
{
    return *m_context.session();
}

void QcControlInventoryCommands::select(const QJsonObject &request)
{
    session().selectHotbarSlot(QcControlArguments::intField(request, u"slot"_s, 0));
    m_context.finish(request, m_context.reports().inventoryJson());
}

void QcControlInventoryCommands::inventory(const QJsonObject &request)
{
    m_context.finish(request, m_context.reports().inventoryJson());
}

void QcControlInventoryCommands::moveItem(const QJsonObject &request)
{
    const quint64 startTick = session().tickCount();
    session().moveInventoryItem(QcControlArguments::intField(request, u"from"_s, 0), QcControlArguments::intField(request, u"to"_s, 0),
                                std::max(0, QcControlArguments::intField(request, u"count"_s, 0)));
    m_context.waitTicks(request, startTick, inventoryTicks);
}

void QcControlInventoryCommands::drop(const QJsonObject &request)
{
    const quint64 startTick = session().tickCount();
    if (!session().dropItem(request.value(u"all"_s).toBool(false))) {
        m_context.fail(request, u"Nothing selected to drop"_s);
        return;
    }

    m_context.waitTicks(request, startTick, inventoryTicks);
}

void QcControlInventoryCommands::recipes(const QJsonObject &request)
{
    // Every recipe with what it needs and how often the inventory could make it right now.
    const auto content = session().content();
    QJsonArray list;
    for (const QcRecipeDefinition &recipe : content ? content->recipes() : QList<QcRecipeDefinition>()) {
        QJsonObject needs;
        const QList<std::pair<QString, int>> needed = QcCrafting::requirements(*content, recipe);
        for (const auto &[ingredient, count] : std::as_const(needed)) {
            needs.insert(ingredient, count);
        }

        const int possible = QcCrafting::timesCraftable(*content, session().inventory(), recipe, 64);
        if (request.value(u"all"_s).toBool(false) || possible > 0) {
            list.append(QJsonObject{{u"recipe"_s, recipe.name},
                                    {u"result"_s, recipe.resultItem},
                                    {u"count"_s, recipe.resultCount},
                                    {u"needs"_s, needs},
                                    {u"needsCraftingTable"_s, QcCrafting::gridSizeNeeded(recipe) > 2},
                                    {u"possible"_s, possible}});
        }
    }

    m_context.finish(request, {{u"recipes"_s, list}});
}

void QcControlInventoryCommands::craft(const QJsonObject &request)
{
    const quint64 startTick = session().tickCount();
    session().craftRecipe(request.value(u"recipe"_s).toString(), std::clamp(QcControlArguments::intField(request, u"count"_s, 1), 1, 64));
    m_context.waitTicks(request, startTick, inventoryTicks);
}

void QcControlInventoryCommands::takeResult(const QJsonObject &request)
{
    const quint64 startTick = session().tickCount();
    session().takeCraftingResult();
    m_context.waitTicks(request, startTick, inventoryTicks);
}

void QcControlInventoryCommands::creativeItem(const QJsonObject &request)
{
    const auto content = session().content();
    const QString name = request.value(u"item"_s).toString();
    const QString qualified = name.contains(u':') ? name : u"qtcraft:"_s + name;
    const std::optional<QcItemId> item = content && !name.isEmpty() ? content->itemId(qualified) : std::nullopt;
    if (!name.isEmpty() && !item) {
        m_context.fail(request, u"Unknown item '%1'"_s.arg(name));
        return;
    }

    const int maxStack = item ? content->item(*item).maxStack : 0;
    const int count = item ? std::clamp(QcControlArguments::intField(request, u"count"_s, maxStack), 1, maxStack) : 0;
    const quint64 startTick = session().tickCount();
    session().gameMode().creativeItem(QcControlArguments::intField(request, u"slot"_s, QcMsg::CreativeItem::addSlot), {item.value_or(qcNoItem), count, 0});
    m_context.waitTicks(request, startTick, inventoryTicks, [this] { return m_context.reports().inventoryJson(); });
}

void QcControlInventoryCommands::pickBlock(const QJsonObject &request)
{
    if (request.contains(u"x"_s)) {
        QcControlAim::lookAt(session(), QcControlArguments::block(request), std::nullopt);
    }

    const quint64 startTick = session().tickCount();
    const std::optional<QcRaycastHit> &target = session().target();
    if (!target || session().isDead()) {
        m_context.fail(request, u"No block in view"_s);
        return;
    }
    session().gameMode().pickBlock(target->block);
    m_context.waitTicks(request, startTick, inventoryTicks, [this] { return m_context.reports().inventoryJson(); });
}

void QcControlInventoryCommands::closeCrafting(const QJsonObject &request)
{
    // Returns the grid to the inventory and leaves a crafting table's 3 x 3 grid.
    const quint64 startTick = session().tickCount();
    session().closeCrafting();
    m_context.waitTicks(request, startTick, inventoryTicks);
}
