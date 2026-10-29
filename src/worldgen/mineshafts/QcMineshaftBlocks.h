// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcBlockDefinition.h"
#include "common/content/definitions/QcMineshaftDefinition.h"
#include "common/content/registry/QcContentRegistry.h"

#include <QStringList>

#include <array>
#include <optional>
#include <vector>

// The blocks a mine shaft places (decision E-570), resolved once from QcMineshaftSettings, and
// which block states the builder treats as open (where a floor gets planks and a pillar goes on
// down) or liquid. Sides are indexed north, east, south, west.
struct QcMineshaftBlocks
{
    QcBlockId planks = qcAirBlockId;
    QcBlockId post = qcAirBlockId;
    // Straight rails along north-south and along east-west.
    QcBlockId railNorthSouth = qcAirBlockId;
    QcBlockId railEastWest = qcAirBlockId;
    // Rails rising towards the side (an inclined entrance's steps, decision E-895).
    std::array<QcBlockId, 4> railsUp{};
    QcBlockId cobweb = qcAirBlockId;
    QcBlockId seal = qcAirBlockId;
    QcBlockId spawner = qcAirBlockId;
    // A light and a ladder hanging on the wall at that side.
    std::array<QcBlockId, 4> lights{};
    std::array<QcBlockId, 4> ladders{};
    // Per block state: nothing to stand on (not solid, or liquid), liquid, falling (sand, gravel)
    // and foliage (solid but not opaque, like leaves and cacti).
    std::vector<bool> open;
    std::vector<bool> liquid;
    std::vector<bool> falling;
    std::vector<bool> foliage;
    // Half sizes of the spawner's spawn area.
    int areaX = 4;
    int areaY = 1;
    int areaZ = 4;

    // The blocks of `settings`; none when one is unknown, with a warning each.
    struct Resolved;
    [[nodiscard]] static Resolved resolve(const QcMineshaftSettings &settings, const QcContentRegistry &content);
    // The side index of a unit step (north, east, south, west).
    static int sideOf(int dx, int dz);
    bool isOpen(QcBlockId block) const { return open[static_cast<std::size_t>(block)]; }
    bool isLiquid(QcBlockId block) const { return liquid[static_cast<std::size_t>(block)]; }
    bool falls(QcBlockId block) const { return falling[static_cast<std::size_t>(block)]; }
    bool isFoliage(QcBlockId block) const { return foliage[static_cast<std::size_t>(block)]; }
};

struct QcMineshaftBlocks::Resolved
{
    std::optional<QcMineshaftBlocks> blocks;
    QStringList warnings;
};
