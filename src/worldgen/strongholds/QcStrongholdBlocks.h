// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/content/worldgen/QcStrongholdDefinition.h"
#include "worldgen/strongholds/QcStrongholdPlan.h"

#include <QStringList>

#include <array>
#include <optional>

/*! \brief The blocks a stronghold places (decision E-1171), resolved once from its settings.

    Arrays by side follow QcStrongholdPlan::Side (north, east, south, west): a door, chest, frame
    or stairs facing that side, a torch, ladder or button attached to the block on that side. */
struct QcStrongholdBlocks
{
    /*! A block by side. */
    using BySide = std::array<QcBlockId, 4>;

    /*! The bricks of the walls: plain, cracked, mossy, chiseled and infested. \qcshared */
    QcBlockId bricks = qcAirBlockId;
    QcBlockId cracked = qcAirBlockId;
    QcBlockId mossy = qcAirBlockId;
    QcBlockId chiseled = qcAirBlockId;
    QcBlockId infested = qcAirBlockId;
    /*! The stairs climbing towards a side, and a bottom slab. \qcshared */
    BySide stairs{};
    QcBlockId slab = qcAirBlockId;
    /*! The lower and upper halves of the doors by side. \qcshared */
    BySide woodDoorLower{};
    BySide woodDoorUpper{};
    BySide ironDoorLower{};
    BySide ironDoorUpper{};
    /*! Bars running along x, and along z. \qcshared */
    QcBlockId barsAlongX = qcAirBlockId;
    QcBlockId barsAlongZ = qcAirBlockId;
    /*! Buttons, ladders and torches by the side they hang on. \qcshared */
    BySide buttons{};
    BySide ladders{};
    BySide torches{};
    /*! The furnishing. \qcshared */
    QcBlockId bookshelf = qcAirBlockId;
    QcBlockId planks = qcAirBlockId;
    QcBlockId fenceAlongX = qcAirBlockId;
    QcBlockId fenceAlongZ = qcAirBlockId;
    QcBlockId cobweb = qcAirBlockId;
    QcBlockId water = qcAirBlockId;
    QcBlockId lava = qcAirBlockId;
    QcBlockId spawner = qcAirBlockId;
    /*! Chests facing a side. */
    BySide chests{};
    /*! End portal frames facing a side, without and with an eye. \qcshared */
    BySide frames{};
    BySide framesWithEye{};

    /*! Returns \a bySide at \a side. */
    [[nodiscard]] static QcBlockId of(BySide bySide, QcStrongholdPlan::Side side) { return bySide[static_cast<std::size_t>(side)]; }
    /*! Returns the bars running across \a side (in a wall facing it). */
    [[nodiscard]] QcBlockId barsAcross(QcStrongholdPlan::Side side) const
    {
        return side == QcStrongholdPlan::Side::North || side == QcStrongholdPlan::Side::South ? barsAlongX : barsAlongZ;
    }
    /*! Returns the fence running across \a side. */
    [[nodiscard]] QcBlockId fenceAcross(QcStrongholdPlan::Side side) const
    {
        return side == QcStrongholdPlan::Side::North || side == QcStrongholdPlan::Side::South ? fenceAlongX : fenceAlongZ;
    }

    struct Resolved;

    /*! Returns the blocks of \a settings resolved in \a content; none when one is unknown, with a
        warning each. */
    [[nodiscard]] static Resolved resolve(const QcStrongholdSettings &settings, const QcContentRegistry &content);
};

/*! \brief The result of QcStrongholdBlocks::resolve(): the blocks, or none when a block is
    unknown, and the warnings. \qcfields */
struct QcStrongholdBlocks::Resolved
{
    std::optional<QcStrongholdBlocks> blocks;
    QStringList warnings;
};
