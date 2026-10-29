// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/session/QcClientWorld.h"
#include "common/core/QcVec3.h"

#include <QList>

#include <optional>
#include <vector>

// What the enchanting tables near the player show in the world (decision E-884), as in the
// reference: a book floating above each table, turning towards the viewer and opening when the
// viewer comes near, and runes flying in an arc from the bookshelves that count (QcEnchantingTable)
// to the table. The tables and their shelves are found again every `rescanTicks` by looking only into
// the chunk sections whose palette holds a table, like the campfire smoke (QcCampfireSmoke). Book and
// runes follow from the positions, the viewer and the time alone, so nothing is kept per rune.
class QcEnchantingAura
{
public:
    // A table and the bookshelves that count for it.
    struct Table
    {
        QcBlockPos pos;
        QList<QcBlockPos> shelves;
    };

    // A rune in flight: where its middle is, which rune (QcRuneGlyphs) and how large (metres).
    struct Rune
    {
        QcVec3 position;
        int rune = 0;
        double size = 0.0;
    };

    // The floating book: the middle of its spine, the direction it faces (radians about +y, 0 facing
    // +z) and how far it is open (0 closed, 1 wide open).
    struct Book
    {
        QcVec3 spine;
        double yaw = 0.0;
        double open = 0.0;
    };

    static constexpr int radius = 24;
    static constexpr int rescanTicks = 20;
    // Tables drawn at most (the nearest).
    static constexpr int maxTables = 8;
    // Seconds between two runes of one bookshelf, and seconds a rune takes to the table.
    static constexpr double cycleSeconds = 3.2;
    static constexpr double flightSeconds = 1.6;
    // The book opens within this distance of the viewer (metres) and is wide open at half of it.
    static constexpr double openDistance = 5.0;

    // Looks for the tables again when `tick` is rescanTicks past the last look or the content changed.
    void refresh(const QcClientWorld &world, const QcVec3 &eye, quint64 tick);
    const QList<Table> &tables() const { return m_tables; }

    // The runes flying to `table` at `seconds` of world time, in a list the aura reuses: valid until
    // the next call.
    [[nodiscard]] const QList<Rune> &runesOf(const Table &table, double seconds);
    // The book above the table at `table` for a viewer at `viewer`.
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
