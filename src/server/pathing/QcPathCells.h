#pragma once

#include "common/content/definitions/QcBlockDefinition.h"

#include <QtGlobal>

#include <vector>

class QcContentRegistry;

// What a block state is to a walking creature looking for a path (decisions E-400, E-402), one byte
// per state, computed once from the content: the kind of cell, two flags and the height of a low
// block. The path grid (QcPathGrid) reads only this table, never the block definitions.
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
// inside (cobwebs).
inline constexpr quint8 hurtsFlag = 0x10;
inline constexpr quint8 slowsFlag = 0x20;
inline constexpr quint8 kindMask = 0x0f;
// The height of a low block's top in eighths of a block, minus one (0: a carpet, 3: a slab).
inline constexpr int liftShift = 6;

inline Kind kindOf(quint8 cell) { return Kind(cell & kindMask); }
// How high a body stands on the low block of the cell (rounded up to an eighth); 0 for other kinds.
inline double liftOf(quint8 cell) { return kindOf(cell) == Kind::Low ? double((cell >> liftShift) + 1) / 8.0 : 0.0; }
inline quint8 make(Kind kind, quint8 flags = 0) { return quint8(quint8(kind) | flags); }

// The cell of one block state.
quint8 classify(const QcContentRegistry &content, QcBlockId id);

} // namespace QcPathCell

// The cells of every block state of a content, indexed by QcBlockId.
class QcPathCellTable
{
public:
    explicit QcPathCellTable(const QcContentRegistry &content);

    // Unknown for ids outside the content.
    quint8 of(QcBlockId state) const { return state < m_cells.size() ? m_cells[state] : QcPathCell::make(QcPathCell::Kind::Unknown); }

private:
    std::vector<quint8> m_cells;
};
