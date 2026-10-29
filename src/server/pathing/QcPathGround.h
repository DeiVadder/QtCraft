// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/pathing/QcPathTerrain.h"

// Where a creature walks: the world, the path cells of its blocks and the creature's path rules.
// What checks a step or a path against the ground (QcMobNavigator::safeStep, pathClear) and what
// steers along it (QcMobNavigator, QcDoorPassage) look at.
struct QcPathGround
{
    const QcBlockAccess &world;
    const QcPathCellTable &cells;
    QcPathRules rules;
};
