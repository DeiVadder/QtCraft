#include "server/gameplay/QcServerCrafting.h"

#include "common/gameplay/QcCrafting.h"
#include "common/gameplay/QcRepair.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcChatText.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcServerInventory.h"

#include <algorithm>
#include <cmath>

QcServerCrafting::QcServerCrafting(QcServerState &state, QcServerInventory &inventory)
    : m_state(state)
    , m_inventory(inventory)
{
}

std::optional<QcItemStack> QcServerCrafting::resultOf(const QcContentRegistry &content, const QcServerClient &client)
{
    if (const std::optional<qsizetype> recipe = craftingMatch(content, client)) {
        const QcRecipeDefinition &definition = content.recipes()[*recipe];
        if (const std::optional<QcItemId> item = content.itemId(definition.resultItem)) {
            return QcItemStack{*item, definition.resultCount, 0};
        }
        return std::nullopt;
    }
    return QcRepair::result(content, craftingView(client));
}

QList<QcItemStack> QcServerCrafting::craftingView(const QcServerClient &client)
{
    QList<QcItemStack> view;
    for (int row = 0; row < client.crafting.size; ++row) {
        for (int column = 0; column < client.crafting.size; ++column) {
            view.append(client.crafting.grid[row * 3 + column]);
        }
    }
    return view;
}

std::optional<qsizetype> QcServerCrafting::craftingMatch(const QcContentRegistry &content, const QcServerClient &client)
{
    return QcCrafting::match(content, craftingView(client), client.crafting.size);
}

bool QcServerCrafting::isNearCraftingTable(const QcServerClient &client) const
{
    const QcBlockPos feet = QcPhysics::blockContaining(client.motion.position);
    const int reach = int(std::ceil(craftingTableReach));

    for (int dx = -reach; dx <= reach; ++dx) {
        for (int dy = -reach; dy <= reach; ++dy) {
            if (hasCraftingTableInColumn(feet, dx, dy)) {
                return true;
            }
        }
    }
    return false;
}

bool QcServerCrafting::hasCraftingTableInColumn(QcBlockPos feet, int dx, int dy) const
{
    const int reach = int(std::ceil(craftingTableReach));
    for (int dz = -reach; dz <= reach; ++dz) {
        const std::optional<QcBlockId> block = m_state.world().blockAt(feet.offset(dx, dy, dz));
        if (block && m_state.content().block(*block).opensCrafting
            && QcVec3{double(dx), double(dy), double(dz)}.length() <= craftingTableReach) {
            return true;
        }
    }
    return false;
}

QcServerCrafting::CraftOutcome QcServerCrafting::craftOnce(QcServerClient &client)
{
    if (client.crafting.size == 3 && !isNearCraftingTable(client)) {
        return CraftOutcome::TooFar;
    }

    const std::optional<qsizetype> recipe = craftingMatch(m_state.content(), client);
    if (!client.dead && !recipe) {
        return repairOnce(client);
    }
    if (client.dead) {
        return CraftOutcome::NoRecipe;
    }

    const QcContentRegistry &content = m_state.content();
    const QcRecipeDefinition &definition = content.recipes()[*recipe];
    const std::optional<QcItemId> item = content.itemId(definition.resultItem);
    QcInventory result = client.inventory;
    if (!item || result.add(*item, definition.resultCount, content) > 0) {
        return CraftOutcome::NoRoom;
    }

    client.inventory = result;
    takeOneOfEachCell(client);
    return CraftOutcome::Crafted;
}

QcServerCrafting::CraftOutcome QcServerCrafting::repairOnce(QcServerClient &client)
{
    const std::optional<QcItemStack> repaired = QcRepair::result(m_state.content(), craftingView(client));
    if (!repaired) {
        return CraftOutcome::NoRecipe;
    }

    QcInventory result = client.inventory;
    if (result.add(repaired->item, 1, m_state.content(), repaired->damage) > 0) {
        return CraftOutcome::NoRoom;
    }

    client.inventory = result;
    takeOneOfEachCell(client);
    return CraftOutcome::Crafted;
}

void QcServerCrafting::takeOneOfEachCell(QcServerClient &client)
{
    for (int row = 0; row < client.crafting.size; ++row) {
        for (int column = 0; column < client.crafting.size; ++column) {
            QcItemStack &cell = client.crafting.grid[row * 3 + column];
            if (!cell.isEmpty() && --cell.count <= 0) {
                cell = {};
            }
        }
    }
}

void QcServerCrafting::takeCraftingResult(QcServerClient &client)
{
    finishCrafting(client, craftOnce(client), false);
}

void QcServerCrafting::craftAll(QcServerClient &client)
{
    // As often as the grid and the inventory allow, at most a full stack of rounds.
    CraftOutcome outcome = CraftOutcome::NoRecipe;
    bool crafted = false;
    for (int round = 0; round < maxCraftAllRounds; ++round) {
        outcome = craftOnce(client);
        if (outcome != CraftOutcome::Crafted) {
            break;
        }
        crafted = true;
    }
    finishCrafting(client, outcome, crafted);
}

void QcServerCrafting::finishCrafting(QcServerClient &client, CraftOutcome outcome, bool craftedBefore)
{
    if (outcome == CraftOutcome::TooFar) {
        closeCrafting(client);
        return;
    }
    if (outcome == CraftOutcome::NoRoom && !craftedBefore) {
        client.connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "Not enough room in the inventory")));
    }
    m_inventory.send(client);
}

void QcServerCrafting::craftRecipe(QcServerClient &client, const QcMsg::CraftRecipe &request)
{
    const QcContentRegistry &content = m_state.content();
    const std::optional<qsizetype> index = content.recipeIndex(request.recipe);
    if (client.dead) {
        return;
    }

    if (!index) {
        client.connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "Unknown recipe %1"),
            {request.recipe}));
        return;
    }

    const QcRecipeDefinition &recipe = content.recipes()[*index];
    if (QcCrafting::gridSizeNeeded(recipe) > 2 && !isNearCraftingTable(client)) {
        client.connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "%1 needs a crafting table nearby"),
            {recipe.name}));
        return;
    }

    const std::optional<QcItemId> result = content.itemId(recipe.resultItem);
    int crafted = 0;
    for (int round = 0; round < std::clamp(request.count, 1, 64) && result; ++round) {
        // All or nothing per round, on a copy so a full inventory leaves everything unchanged.
        QcInventory attempt = client.inventory;
        if (!QcCrafting::consume(content, attempt, recipe, 1) || attempt.add(*result, recipe.resultCount, content) > 0) {
            break;
        }

        client.inventory = attempt;
        ++crafted;
    }

    if (crafted > 0) {
        client.connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "Crafted %1 x %2"),
                                                    {QString::number(crafted * recipe.resultCount), recipe.resultItem}));
    } else {
        client.connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "Missing ingredients or room for %1"), {recipe.name}));
    }
    m_inventory.send(client);
}

void QcServerCrafting::openCraftingTable(QcServerClient &client, QcBlockPos pos)
{
    client.crafting.size = 3;
    client.crafting.table = pos;
    client.connection->send(QcMsg::OpenCrafting{3});
    m_inventory.send(client);
}

void QcServerCrafting::closeCrafting(QcServerClient &client)
{
    returnCraftingGrid(client);
    client.crafting.size = 2;
    client.crafting.table.reset();
    m_inventory.send(client);
}

void QcServerCrafting::returnCraftingGrid(QcServerClient &client)
{
    // Catalogue items that do not fit go into the bin rather than into the world (decision E-283).
    const bool catalogue = QcGameModes::rules(client.gameMode).itemCatalogue;
    for (QcItemStack &cell : client.crafting.grid) {
        if (cell.isEmpty()) {
            continue;
        }

        if (catalogue) {
            client.inventory.add(cell.item, cell.count, m_state.content(), cell.damage);
        } else {
            m_inventory.giveOrThrow(client, cell);
        }
        cell = {};
    }
}
