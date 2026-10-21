#pragma once

#include "common/core/QcBlockPos.h"
#include "common/gameplay/QcItemStack.h"
#include "common/protocol/QcMessages.h"

#include <QList>

#include <optional>

class QcContentRegistry;
class QcServerInventory;
struct QcServerClient;
class QcServerState;

// Crafting of the players (decision E-367): the 2 x 2 grid of the inventory and the 3 x 3 grid of a
// crafting table near them, recipes and repairs taken from the grid (once or as often as possible),
// recipes crafted straight from the inventory (CraftRecipe) and the grid's items going back into the
// inventory when it closes.
class QcServerCrafting
{
public:
    // Crafting needs a crafting table within this distance for 3 x 3 recipes.
    static constexpr double craftingTableReach = 5.0;
    // Shift-clicks on a crafting result take it as often as possible, at most this many times.
    static constexpr int maxCraftAllRounds = 64;

    QcServerCrafting(QcServerState &state, QcServerInventory &inventory);

    // What the player's grid makes now (a recipe, a repair or a map copied or zoomed out).
    [[nodiscard]] static std::optional<QcItemStack> resultOf(const QcServerState &state, const QcServerClient &client);

    void takeCraftingResult(QcServerClient &client);
    void craftAll(QcServerClient &client);
    void craftRecipe(QcServerClient &client, const QcMsg::CraftRecipe &request);
    // Opens the 3 x 3 grid of the crafting table at `pos`.
    void openCraftingTable(QcServerClient &client, QcBlockPos pos);
    void closeCrafting(QcServerClient &client);
    // Moves the crafting grid into the inventory; what does not fit is thrown on the ground, or
    // deleted in a mode with the item catalogue.
    void returnCraftingGrid(QcServerClient &client);

private:
    enum class CraftOutcome { Crafted, NoRecipe, NoRoom, TooFar };

    // The grid of the current size as a size x size list, and the recipe it makes.
    static QList<QcItemStack> craftingView(const QcServerClient &client);
    [[nodiscard]] static std::optional<qsizetype> craftingMatch(const QcContentRegistry &content, const QcServerClient &client);
    // Takes the crafting result once.
    CraftOutcome craftOnce(QcServerClient &client);
    // Takes the repair result (QcRepair) once.
    // Copying or zooming out a map in the grid (decision E-804).
    CraftOutcome mapOnce(QcServerClient &client);
    CraftOutcome repairOnce(QcServerClient &client);
    // Takes one item from each cell; a container (a milk bucket's bucket) stays in a cell it empties.
    void takeOneOfEachCell(QcServerClient &client);
    // The containers that taking one of each cell leaves for the inventory: those of cells that keep
    // items (decision E-771).
    [[nodiscard]] QList<QcItemStack> containersForInventory(const QcServerClient &client) const;
    void finishCrafting(QcServerClient &client, CraftOutcome outcome, bool craftedBefore);
    bool isNearCraftingTable(const QcServerClient &client) const;
    // Whether a crafting table stands in the column (dx, dy) around `feet` within reach.
    bool hasCraftingTableInColumn(QcBlockPos feet, int dx, int dy) const;

    QcServerState &m_state;
    QcServerInventory &m_inventory;
};
