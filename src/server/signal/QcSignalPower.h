#pragma once

#include "QcBlockAccess.h"
#include "QcSignalTable.h"

#include <QVarLengthArray>

// Who powers what (decision E-333), read from the world as it is. A source sends power directly
// into its neighbouring components and dust (a lever into all six, a torch into all but its block,
// a repeater only forwards) and strongly into a conductor (the block a lever or button hangs on,
// the block under a plate, above a torch, in front of a repeater). Dust powers the block it lies on
// and the blocks its lines point at weakly. A strongly powered conductor powers neighbouring dust
// and components; a weakly powered one only components, never dust, so dust does not feed itself
// through blocks. All answers are maxima over the six faces: the order of the faces never matters.
class QcSignalPower
{
public:
    using Positions = QVarLengthArray<QcBlockPos, 8>;

    QcSignalPower(const QcBlockAccess &world, const QcSignalTable &table);

    const QcSignalState &at(QcBlockPos pos) const;
    [[nodiscard]] std::optional<QcBlockId> blockAt(QcBlockPos pos) const { return m_world.blockAt(pos); }
    const QcSignalTable &table() const { return m_table; }
    const QcBlockAccess &world() const { return m_world; }

    // Power the block `state` at `from` sends straight into the neighbouring cell towards `towards`.
    static int directPower(QcSignalState state, QcBlockFace towards);
    // Strong power the block `state` puts into the conductor towards `towards`.
    static int strongPower(QcSignalState state, QcBlockFace towards);

    // Strongest strong power a conductor at `pos` gets from its neighbours (0 for other blocks).
    int strongLevel(QcBlockPos pos) const;
    // Strong level or what dust puts into the conductor at `pos`.
    int weakLevel(QcBlockPos pos) const;
    // What a component at `pos` gets through the face `face`.
    int inputFrom(QcBlockPos pos, QcBlockFace face) const;
    // What a component at `pos` gets through every face but those whose bit (1 << face) is set.
    int input(QcBlockPos pos, quint8 ignoredFaces = 0) const;
    // What dust at `pos` gets from outside its wire: sources and strongly powered blocks.
    int dustInput(QcBlockPos pos) const;

    // An unloaded chunk within two blocks of `pos` (what a component or dust there reads may lie in
    // it), if any: circuits wait for it instead of reading it as air (decision E-335).
    [[nodiscard]] std::optional<QcChunkPos> unloadedNear(QcBlockPos pos) const;

    // Cells beyond its six neighbours that the block `state` at `pos` powers: its strong target and
    // the blocks dust points at. Their neighbours notice when it changes.
    static Positions targets(QcBlockPos pos, QcSignalState state);

private:
    const QcBlockAccess &m_world;
    const QcSignalTable &m_table;
};
