// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/core/QcBlockFace.h"

#include <QHash>

#include <array>
#include <optional>
#include <vector>

class QcContentRegistry;

// How a side of a Signal Dust block connects: not at all, flat (also down a block), or up the
// neighbouring block.
enum class QcDustSide : quint8 { None, Side, Up };

// What the signal engine reads from one block state, decoded once from the state's properties so
// that circuits never compare strings while they run (decision E-335). Indexed by block state id.
struct QcSignalState
{
    QcSignalKind kind = QcSignalKind::None;
    // Dust and comparators: their strength. Sources: what they emit while on (0 while off).
    quint8 power = 0;
    // Levers, buttons, plates, repeaters, comparators and observers: `powered`; torches and lamps:
    // the lit block; pistons: `extended`; openers: `open`; piston heads: `sticky`; dispensers:
    // `triggered`.
    bool on = false;
    // Repeaters and pistons: where they point; torches, levers and buttons: towards the block they
    // hang on (down on the floor, up at the ceiling).
    std::optional<QcBlockFace> facing;
    quint8 delay = 1;
    // Repeaters: locked by a side; hoppers: not `enabled` (powered).
    bool locked = false;
    // Comparators in subtract mode.
    bool subtract = false;
    // Torches: whether a lit one strongly powers a conductor above it
    // (QcSignalDefinition::strongPowersAbove).
    bool strongPowersAbove = true;
    // Dust connections towards north, east, south and west.
    std::array<QcDustSide, 4> sides{};
    // A full solid opaque block that passes power (stone, lamps), whatever its kind.
    bool conductor = false;
    QcPistonMobility mobility = QcPistonMobility::Push;
};

class QcSignalTable
{
public:
    // North, east, south and west, the order of QcSignalState::sides.
    static constexpr std::array<QcBlockFace, 4> horizontal{QcBlockFace::North, QcBlockFace::East, QcBlockFace::South,
                                                          QcBlockFace::West};
    static int horizontalIndex(QcBlockFace face);

    explicit QcSignalTable(const QcContentRegistry &content);

    const QcSignalState &at(QcBlockId id) const
    {
        return static_cast<std::size_t>(id) < m_states.size() ? m_states[static_cast<std::size_t>(id)] : m_none;
    }
    const QcContentRegistry &content() const { return m_content; }

    // The dust state of `dust` (any state of it) with this strength and these sides.
    QcBlockId dustState(QcBlockId dust, int power, std::array<QcDustSide, 4> sides) const;
    // `id` with `property` set to a boolean.
    QcBlockId withFlag(QcBlockId id, const QString &property, bool value) const;
    // The same state of the block's counterpart (lit and dark torch or lamp), or `id` without one.
    QcBlockId counterpartOf(QcBlockId id) const;

private:
    // Where strength and sides sit in the state index of a dust block, so that dustState is plain
    // arithmetic: index = sum of value index times stride (the first property varies fastest).
    struct DustLayout
    {
        QcBlockId first = 0;
        int powerStride = 0;
        std::array<int, QcSignalDefinition::maxPower + 1> powerValue{};
        std::array<int, 4> sideStride{};
        std::array<std::array<int, 3>, 4> sideValue{};

        // Notes where `property` sits in the layout; false when it is neither the strength nor a side.
        bool place(const QcBlockProperty &property, int stride);
    };

    static QcSignalState decode(const QcContentRegistry &content, QcBlockId id);
    [[nodiscard]] static std::optional<DustLayout> dustLayout(const QcContentRegistry &content, quint32 type);

    const QcContentRegistry &m_content;
    std::vector<QcSignalState> m_states;
    QcSignalState m_none;
    // Per dust block type.
    QHash<quint32, DustLayout> m_dust;
};
