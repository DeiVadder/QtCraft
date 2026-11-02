// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/session/QcClientWorld.h"
#include "common/core/QcVec3.h"

#include <QList>

#include <optional>
#include <vector>

/*! \brief What the enchanting tables near the player show in the world (decision E-884), as in the
    reference: a book floating above each table, turning towards the viewer and opening when the
    viewer comes near, and runes flying in an arc from the bookshelves that count
    (QcEnchantingTable) to the table.

    The tables and their shelves are found again every \c {rescanTicks} by looking only into the
    chunk sections whose palette holds a table, like the campfire smoke (QcCampfireSmoke). Book
    and runes follow from the positions, the viewer and the time alone, so nothing is kept per
    rune. */
class QcEnchantingAura
{
public:
    /*! \brief A table and the bookshelves that count for it. */
    struct Table
    {
        /*! Where the table is. */
        QcBlockPos pos;
        /*! The bookshelves that count for it. */
        QList<QcBlockPos> shelves;
    };

    /*! \brief A rune in flight: where its middle is, which rune (QcRuneGlyphs) and how large
        (metres). */
    struct Rune
    {
        /*! Where the rune's middle is. */
        QcVec3 position;
        /*! Which rune it is (an index into QcRuneGlyphs). */
        int rune = 0;
        /*! Its size in metres. */
        double size = 0.0;
    };

    /*! \brief The floating book: the middle of its spine, the direction it faces (radians about
        +y, 0 facing +z) and how far it is open (0 closed, 1 wide open). */
    struct Book
    {
        /*! The middle of the book's spine. */
        QcVec3 spine;
        /*! The direction the book faces, in radians about +y (0 facing +z). */
        double yaw = 0.0;
        /*! How far the book is open: 0 closed, 1 wide open. */
        double open = 0.0;
    };

    /*! Blocks around the eye within which tables are found. */
    static constexpr int radius = 24;
    /*! Ticks between two looks for the tables. */
    static constexpr int rescanTicks = 20;
    /*! Tables drawn at most (the nearest). */
    static constexpr int maxTables = 8;
    /*! Seconds between two runes of one bookshelf, and seconds a rune takes to the table.
        \qcshared */
    static constexpr double cycleSeconds = 3.2;
    static constexpr double flightSeconds = 1.6;
    /*! The book opens within this distance of the viewer (metres) and is wide open at half of
        it. */
    static constexpr double openDistance = 5.0;

    /*! Looks for the tables of \a world around \a eye again when \a tick is rescanTicks past the
        last look or the content changed. */
    void refresh(const QcClientWorld &world, const QcVec3 &eye, quint64 tick);
    /*! Returns the tables found by the latest look, the nearest at most maxTables. */
    const QList<Table> &tables() const { return m_tables; }

    /*! Returns the runes flying to \a table at \a seconds of world time, in a list the aura
        reuses: valid until the next call. */
    [[nodiscard]] const QList<Rune> &runesOf(const Table &table, double seconds);
    /*! Returns the book above the table at \a table for a viewer at \a viewer at \a seconds of
        world time. */
    [[nodiscard]] static Book bookOf(QcBlockPos table, const QcVec3 &viewer, double seconds);

private:
    [[nodiscard]] QList<Table> scan(const QcClientWorld &world, const QcVec3 &eye) const;
    // The tables in one section whose first cell is at `origin`; none without one in its palette.
    [[nodiscard]] QList<QcBlockPos> tablesIn(const QcChunkSection &section, QcBlockPos origin) const;
    [[nodiscard]] bool isTable(QcBlockId id) const;
    // Which block states are enchanting tables in the world's content.
    void learn(const QcContentRegistry &content);

    std::vector<bool> m_isTable;
    const QcContentRegistry *m_content = nullptr;
    QList<Table> m_tables;
    // The runes of the last runesOf, kept so a frame does not allocate a list per table.
    QList<Rune> m_runes;
    quint64 m_scannedAt = 0;
    bool m_scanned = false;
};
