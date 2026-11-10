// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/content/worldgen/QcNetherDefinition.h"
#include "worldgen/bastion/QcBastionPlan.h"

#include <QStringList>

#include <array>
#include <optional>
#include <vector>

/*! \brief The blocks a bastion places (decision E-1087), resolved once from its settings, and which
    block states count as open: what a foundation passes on its way to the rock.

    Arrays by side follow QcBastionPlan::Side. */
struct QcBastionBlocks
{
    /*! The stone of a bastion. \qcshared */
    QcBlockId rough = qcAirBlockId;
    QcBlockId brick = qcAirBlockId;
    QcBlockId cracked = qcAirBlockId;
    QcBlockId polished = qcAirBlockId;
    QcBlockId chiseled = qcAirBlockId;
    QcBlockId gilded = qcAirBlockId;
    /*! The gold, the soil of a stall and the lava. \qcshared */
    QcBlockId gold = qcAirBlockId;
    QcBlockId soil = qcAirBlockId;
    QcBlockId lava = qcAirBlockId;
    /*! The lantern standing on the floor. */
    QcBlockId lantern = qcAirBlockId;
    /*! The spawner. */
    QcBlockId spawner = qcAirBlockId;
    /*! A chest with its front to the side, and stairs climbing towards it. \qcshared */
    std::array<QcBlockId, 4> chests{};
    std::array<QcBlockId, 4> stairs{};
    /*! The wall running along x and along z. \qcshared */
    QcBlockId wallAlongX = qcAirBlockId;
    QcBlockId wallAlongZ = qcAirBlockId;
    /*! Per block state: nothing to stand on (not solid, or liquid). */
    std::vector<bool> open;

    /*! Returns whether \a block offers nothing to stand on. */
    [[nodiscard]] bool isOpen(QcBlockId block) const { return block < open.size() && open[block]; }
    /*! Returns the wall that runs along the axis of \a side. */
    [[nodiscard]] QcBlockId wallAlong(QcBastionPlan::Side side) const
    {
        return side == QcBastionPlan::Side::East || side == QcBastionPlan::Side::West ? wallAlongX : wallAlongZ;
    }

    struct Resolved;

    /*! Returns the blocks of \a settings resolved in \a content; none when one is unknown, with a
        warning each. */
    [[nodiscard]] static Resolved resolve(const QcNetherSettings::Bastion &settings, const QcContentRegistry &content);
};

/*! \brief The result of QcBastionBlocks::resolve(): the blocks, or none when a block is unknown,
    and the warnings. \qcfields */
struct QcBastionBlocks::Resolved
{
    std::optional<QcBastionBlocks> blocks;
    QStringList warnings;
};
