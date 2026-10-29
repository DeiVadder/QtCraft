// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QStringList>

struct QcShowcasePlan;

// The camera stops of the showcase screenshot tour (scripts/showcase-tour.sh), one line each:
// "<name> <x> <z> <yaw> <pitch>" (stand on top of column x, z and look along yaw and pitch).
namespace QcShowcaseTour {

QStringList lines(const QcShowcasePlan &plan);

} // namespace QcShowcaseTour
