// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/content/worldgen/QcNetherDefinition.h"
#include "worldgen/fortress/QcFortressPlan.h"

#include <QStringList>

#include <array>
#include <optional>
#include <vector>

/*! \brief The blocks a fortress places (decision E-1070), resolved once from its settings, and
    which block states count as open: what a pier passes on its way to the rock.

    Arrays by side follow QcFortressPlan::Side. */
struct QcFortressBlocks
{
    /*! The walls, their worn cells and their ribs, and the middle lane of the floors. \qcshared */
    QcBlockId brick = qcAirBlockId;
    QcBlockId cracked = qcAirBlockId;
    QcBlockId chiseled = qcAirBlockId;
    QcBlockId lane = qcAirBlockId;
    /*! The block in the roofs that gives light. */
    QcBlockId light = qcAirBlockId;
    /*! The soil of a bed, and the crop on it by age. \qcshared */
    QcBlockId soil = qcAirBlockId;
    std::array<QcBlockId, 4> crops{};
    /*! The spawner. */
    QcBlockId spawner = qcAirBlockId;
    /*! A chest with its front to the side, and stairs climbing towards it. \qcshared */
    std::array<QcBlockId, 4> chests{};
    std::array<QcBlockId, 4> stairs{};
    /*! The fence running along x, along z, and a post on its own. \qcshared */
    QcBlockId fenceAlongX = qcAirBlockId;
    QcBlockId fenceAlongZ = qcAirBlockId;
    QcBlockId fencePost = qcAirBlockId;
    /*! Per block state: nothing to stand on (not solid, or liquid). */
    std::vector<bool> open;

    /*! Returns whether \a block offers nothing to stand on. */
    [[nodiscard]] bool isOpen(QcBlockId block) const { return block < open.size() && open[block]; }
    /*! Returns the stairs climbing towards \a side. */
    [[nodiscard]] QcBlockId stairsTo(QcFortressPlan::Side side) const { return stairs[static_cast<std::size_t>(side)]; }
    /*! Returns the chest with its front to \a side. */
    [[nodiscard]] QcBlockId chestTo(QcFortressPlan::Side side) const { return chests[static_cast<std::size_t>(side)]; }
    /*! Returns the fence that runs along the axis of \a side. */
    [[nodiscard]] QcBlockId fenceAlong(QcFortressPlan::Side side) const
    {
        return side == QcFortressPlan::Side::East || side == QcFortressPlan::Side::West ? fenceAlongX : fenceAlongZ;
    }

    struct Resolved;

    /*! Returns the blocks of \a settings resolved in \a content; none when one is unknown, with a
        warning each. */
    [[nodiscard]] static Resolved resolve(const QcNetherSettings::Fortress &settings, const QcContentRegistry &content);
};

/*! \brief The result of QcFortressBlocks::resolve(): the blocks, or none when a block is unknown,
    and the warnings. \qcfields */
struct QcFortressBlocks::Resolved
{
    std::optional<QcFortressBlocks> blocks;
    QStringList warnings;
};
