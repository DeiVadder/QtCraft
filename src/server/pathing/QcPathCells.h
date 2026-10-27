#pragma once

#include "common/content/definitions/QcBlockDefinition.h"

#include <QtGlobal>

#include <algorithm>
#include <optional>
#include <vector>

class QcContentRegistry;

// A free stretch of a cell along one horizontal axis, in block units from the cell's corner.
struct QcPathOpening
{
    float from = 0.0f;
    float to = 1.0f;

    float middle() const { return (from + to) / 2.0f; }
    // The stretch free in both: where they do not overlap, the empty stretch between their inner
    // edges (its middle lies as far from both panels as can be).
    QcPathOpening within(QcPathOpening other) const { return {std::max(from, other.from), std::min(to, other.to)}; }
};

// Where a body goes through the cell of an open door or trapdoor (decision E-905): per horizontal
// axis, the free opening beside its panel; none along an axis that nothing narrows. A panel narrows
// the axis across it: an open door's leaf standing along z narrows x.
struct QcPathGap
{
    std::optional<QcPathOpening> x;
    std::optional<QcPathOpening> z;

    bool narrows() const { return x || z; }
    // Both gaps at once (a body in two cells): the openings within each other along each axis.
    QcPathGap within(const QcPathGap &other) const { return {both(x, other.x), both(z, other.z)}; }

private:
    static std::optional<QcPathOpening> both(std::optional<QcPathOpening> a, std::optional<QcPathOpening> b)
    {
        return a && b ? a->within(*b) : a ? a : b;
    }
};

// What a block state is to a walking creature looking for a path (decisions E-400, E-402), one byte
// per state, computed once from the content: the kind of cell, two flags and the height of a low
// block (or, for a solid block, whether it is slippery). The path grid (QcPathGrid) reads only this table, never the block definitions.
namespace QcPathCell {

enum class Kind : quint8 {
    // No collision box (air, plants, torches, open doors, gates and trapdoors).
    Open,
    // Collision only in the lower half (bottom slabs, carpets, snow layers, closed bottom
    // trapdoors): a body stands in the cell, on top of it (liftOf).
    Low,
    // Collision reaching into the upper half over most of the cell's ground (full blocks, stairs,
    // closed top trapdoors): a floor, never passable.
    Solid,
    // Collision reaching into the cell above (fences, walls): neither passable nor a floor, and it
    // also blocks the cell above.
    Tall,
    // A liquid without contact damage: passable, a floor at its surface, avoided by walkers.
    Water,
    // A closed wooden door or gate: blocks, unless the creature opens doors (QcPathRules::opensDoors).
    ClosedDoor,
    // Hurts on contact without being a floor (fire, lava): never entered.
    Danger,
    // Not loaded or outside the world: never entered, never a floor.
    Unknown,
    // Collision over less than half of the cell's ground that leaves no room for a body (panes,
    // iron bars, closed iron doors that only a signal opens): never passable, never a floor.
    Wall,
    // A ladder: its board at the side leaves room for a body, which holds on to it (a place to
    // stand) and climbs up and down along it, if the creature climbs (QcPathRules::climbsLadders;
    // otherwise it is open).
    Ladder,
};

// Flags beside the kind: the block hurts bodies touching it (a floor of magma), it slows bodies
// inside (cobwebs). A solid block can also be slippery (ice, decision E-430): that flag shares its
// bit with the height of low blocks, which solid ones do not have.
inline constexpr quint8 hurtsFlag = 0x10;
inline constexpr quint8 slowsFlag = 0x20;
inline constexpr quint8 slipperyFlag = 0x40;
inline constexpr quint8 kindMask = 0x0f;
// The height of a low block's top in eighths of a block, minus one (0: a carpet, 3: a slab).
inline constexpr int liftShift = 6;

inline Kind kindOf(quint8 cell) { return Kind(cell & kindMask); }
// How high a body stands on the low block of the cell (rounded up to an eighth); 0 for other kinds.
inline double liftOf(quint8 cell) { return kindOf(cell) == Kind::Low ? static_cast<double>((cell >> liftShift) + 1) / 8.0 : 0.0; }
inline quint8 make(Kind kind, quint8 flags = 0) { return static_cast<quint8>(static_cast<quint8>(kind) | flags); }

// The cell of one block state.
quint8 classify(const QcContentRegistry &content, QcBlockId id);
// The gap of one block state: open doors and trapdoors have one beside their panel, every other state none.
QcPathGap gapOf(const QcContentRegistry &content, QcBlockId id);

} // namespace QcPathCell

// The cells of every block state of a content, indexed by QcBlockId.
class QcPathCellTable
{
public:
    explicit QcPathCellTable(const QcContentRegistry &content);

    // Unknown for ids outside the content.
    quint8 of(QcBlockId state) const { return state < m_cells.size() ? m_cells[state] : QcPathCell::make(QcPathCell::Kind::Unknown); }
    // No gap for ids outside the content.
    QcPathGap gap(QcBlockId state) const { return state < m_gaps.size() ? m_gaps[state] : QcPathGap{}; }

private:
    std::vector<quint8> m_cells;
    std::vector<QcPathGap> m_gaps;
};
