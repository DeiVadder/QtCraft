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

/*! \brief How a side of a Signal Dust block connects: not at all, flat (also down a block), or
    up the neighbouring block. */
enum class QcDustSide : quint8 {
    /*! Not connected. */
    None,
    /*! Flat to the side (also down a block). */
    Side,
    /*! Up the neighbouring block. */
    Up
};

/*! \brief What the signal engine reads from one block state, decoded once from the state's
    properties so that circuits never compare strings while they run (decision E-335).

    Indexed by block state id. */
struct QcSignalState
{
    /*! What kind of component it is. */
    QcSignalKind kind = QcSignalKind::None;
    /*! Dust and comparators: their strength. Sources: what they emit while on (0 while off). */
    quint8 power = 0;
    /*! Levers, buttons, plates, repeaters, comparators and observers: \c {powered}; torches and
        lamps: the lit block; pistons: \c {extended}; openers: \c {open}; piston heads:
        \c {sticky}; dispensers: \c {triggered}. */
    bool on = false;
    /*! Repeaters and pistons: where they point; torches, levers and buttons: towards the block
        they hang on (down on the floor, up at the ceiling). */
    std::optional<QcBlockFace> facing;
    /*! Repeaters: their delay in steps. */
    quint8 delay = 1;
    /*! Repeaters: locked by a side; hoppers: not \c {enabled} (powered). */
    bool locked = false;
    /*! Comparators in subtract mode. */
    bool subtract = false;
    /*! Torches: whether a lit one strongly powers a conductor above it
        (QcSignalDefinition::strongPowersAbove). */
    bool strongPowersAbove = true;
    /*! Dust connections towards north, east, south and west. */
    std::array<QcDustSide, 4> sides{};
    /*! A full solid opaque block that passes power (stone, lamps), whatever its kind. */
    bool conductor = false;
    /*! How a piston moves the block. */
    QcPistonMobility mobility = QcPistonMobility::Push;
};

/*! \brief The QcSignalState of every block state of a content, indexed by block state id, and
    the states circuits switch to. */
class QcSignalTable
{
public:
    /*! North, east, south and west, the order of QcSignalState::sides. */
    static constexpr std::array<QcBlockFace, 4> horizontal{QcBlockFace::North, QcBlockFace::East, QcBlockFace::South,
                                                          QcBlockFace::West};
    /*! Returns the index of \a face in horizontal, or -1 for up and down. */
    static int horizontalIndex(QcBlockFace face);

    explicit QcSignalTable(const QcContentRegistry &content);

    /*! Returns the signal state of \a id; one of no kind for an unknown id. */
    const QcSignalState &at(QcBlockId id) const
    {
        return static_cast<std::size_t>(id) < m_states.size() ? m_states[static_cast<std::size_t>(id)] : m_none;
    }
    /*! Returns the content the table was decoded from. */
    const QcContentRegistry &content() const { return m_content; }

    /*! Returns the dust state of \a dust (any state of it) with strength \a power and \a sides. */
    QcBlockId dustState(QcBlockId dust, int power, std::array<QcDustSide, 4> sides) const;
    /*! Returns \a id with \a property set to the boolean \a value. */
    QcBlockId withFlag(QcBlockId id, const QString &property, bool value) const;
    /*! Returns the state of the counterpart of \a id's block (lit and dark torch or lamp) with the
        same property values, or \a id when the block has none. */
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
