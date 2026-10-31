// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcBlockAccess.h"
#include "server/signal/QcSignalTable.h"

#include <QVarLengthArray>

/*! \brief Who powers what (decision E-333), read from the world as it is.

    A source sends power directly into its neighbouring components and dust (a lever into all
    six, a torch into all but its block, a repeater only forwards) and strongly into a conductor
    (the block a lever or button hangs on, the block under a plate, above a torch but not a small
    one, in front of a repeater). Dust powers the block it lies on and the blocks its lines point
    at weakly. A strongly powered conductor powers neighbouring dust and components; a weakly
    powered one only components, never dust, so dust does not feed itself through blocks. All
    answers are maxima over the six faces: the order of the faces never matters. */
class QcSignalPower
{
public:
    /*! A short list of positions. */
    using Positions = QVarLengthArray<QcBlockPos, 8>;

    QcSignalPower(const QcBlockAccess &world, const QcSignalTable &table);

    /*! Returns the signal state of the block at \a pos (that of air where nothing is loaded). */
    const QcSignalState &at(QcBlockPos pos) const;
    /*! Returns the block at \a pos, or nothing where it is not loaded. */
    [[nodiscard]] std::optional<QcBlockId> blockAt(QcBlockPos pos) const { return m_world.blockAt(pos); }
    /*! Returns the signal table. */
    const QcSignalTable &table() const { return m_table; }
    /*! Returns the world it reads. */
    const QcBlockAccess &world() const { return m_world; }

    /*! Returns the power the block \a state sends straight into the neighbouring cell towards
        \a towards. */
    static int directPower(QcSignalState state, QcBlockFace towards);
    /*! Returns the strong power the block \a state puts into the conductor towards \a towards. */
    static int strongPower(QcSignalState state, QcBlockFace towards);

    /*! Returns the strongest strong power a conductor at \a pos gets from its neighbours (0 for
        other blocks). */
    int strongLevel(QcBlockPos pos) const;
    /*! Returns the strong level or what dust puts into the conductor at \a pos, whichever is
        higher. */
    int weakLevel(QcBlockPos pos) const;
    /*! Returns what a component at \a pos gets through the face \a face. */
    int inputFrom(QcBlockPos pos, QcBlockFace face) const;
    /*! Returns what a component at \a pos gets through every face but those whose bit
        (1 << face) is set in \a ignoredFaces. */
    int input(QcBlockPos pos, quint8 ignoredFaces = 0) const;
    /*! Returns what dust at \a pos gets from outside its wire: sources and strongly powered
        blocks. */
    int dustInput(QcBlockPos pos) const;

    /*! Returns an unloaded chunk within two blocks of \a pos (what a component or dust there
        reads may lie in it), if any: circuits wait for it instead of reading it as air (decision
        E-335). */
    [[nodiscard]] std::optional<QcChunkPos> unloadedNear(QcBlockPos pos) const;

    /*! Returns the cells beyond its six neighbours that the block \a state at \a pos powers: its
        strong target and the blocks dust points at. Their neighbours notice when it changes. */
    static Positions targets(QcBlockPos pos, QcSignalState state);

private:
    const QcBlockAccess &m_world;
    const QcSignalTable &m_table;
};
