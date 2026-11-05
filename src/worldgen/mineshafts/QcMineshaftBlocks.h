// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/content/worldgen/QcMineshaftDefinition.h"

#include <QStringList>

#include <array>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

/*! \brief The blocks a mine shaft places (decision E-570), resolved once from QcMineshaftSettings,
    and which block states the builder treats as open (where a floor gets planks and a pillar goes
    on down) or liquid.

    Sides are indexed north, east, south, west. */
struct QcMineshaftBlocks
{
    /*! The planks of floors over open cells, support beams and the entrances' platforms. */
    QcBlockId planks = qcAirBlockId;
    /*! The posts of supports and pillars. */
    QcBlockId post = qcAirBlockId;
    /*! Straight rails along north-south and along east-west. \qcshared */
    QcBlockId railNorthSouth = qcAirBlockId;
    QcBlockId railEastWest = qcAirBlockId;
    /*! Rails rising towards the side (an inclined entrance's steps, decision E-895). */
    std::array<QcBlockId, 4> railsUp{};
    /*! The cobweb block. */
    QcBlockId cobweb = qcAirBlockId;
    /*! The block that replaces liquid beside a corridor and sand or gravel above it. */
    QcBlockId seal = qcAirBlockId;
    /*! The spawner block. */
    QcBlockId spawner = qcAirBlockId;
    /*! A light and a ladder hanging on the wall at that side. \qcshared */
    std::array<QcBlockId, 4> lights{};
    std::array<QcBlockId, 4> ladders{};
    /*! Per block state: nothing to stand on (not solid, or liquid), liquid, falling (sand,
        gravel) and foliage (solid but not opaque, like leaves and cacti). \qcshared */
    std::vector<bool> open;
    std::vector<bool> liquid;
    std::vector<bool> falling;
    std::vector<bool> foliage;
    /*! Half sizes of the spawner's spawn area along x, y and z (half the spawner block's area,
        rounded down). \qcshared */
    int areaX = 4;
    int areaY = 1;
    int areaZ = 4;

    struct Resolved;
    /*! Returns the blocks of \a settings resolved in \a content; none when one is unknown, with
        a warning each. */
    [[nodiscard]] static Resolved resolve(const QcMineshaftSettings &settings, const QcContentRegistry &content);
    /*! \brief A side of a cell; the value is its index in the arrays by side (lights, ladders,
        railsUp) and the number of quarter turns from north. */
    enum class Side : quint8
    {
        /*! Towards -z. */
        North = 0,
        /*! Towards +x. */
        East = 1,
        /*! Towards +z. */
        South = 2,
        /*! Towards -x. */
        West = 3,
    };
    /*! Returns the side of the unit step \a dx, \a dz. */
    static Side sideOf(int dx, int dz);
    /*! Returns the index of \a side in the arrays by side and its quarter turns from north. */
    static constexpr std::size_t slotOf(Side side) { return std::to_underlying(side); }
    /*! Returns whether \a block offers nothing to stand on (not solid, or liquid). */
    bool isOpen(QcBlockId block) const { return flagOf(open, block); }
    /*! Returns whether \a block is a liquid. */
    bool isLiquid(QcBlockId block) const { return flagOf(liquid, block); }
    /*! Returns whether \a block falls (sand, gravel). */
    bool falls(QcBlockId block) const { return flagOf(falling, block); }
    /*! Returns whether \a block is foliage (solid but not opaque). */
    bool isFoliage(QcBlockId block) const { return flagOf(foliage, block); }

    /*! Returns the entry of \a block in \a table (one per runtime id of the content). */
    static bool flagOf(const std::vector<bool> &table, QcBlockId block)
    {
        Q_ASSERT_X(std::cmp_less(block, table.size()), "QcMineshaftBlocks::flagOf", "runtime id out of range");
        return table[static_cast<std::size_t>(block)];
    }
};

/*! \brief The result of QcMineshaftBlocks::resolve(): the blocks, or none when a block is
    unknown, and the warnings. \qcfields */
struct QcMineshaftBlocks::Resolved
{
    std::optional<QcMineshaftBlocks> blocks;
    QStringList warnings;
};
