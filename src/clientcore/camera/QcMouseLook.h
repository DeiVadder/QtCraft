// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/physics/QcPlayerInput.h"

#include <QPointF>

// Turns relative mouse motion (in points, y down) into view yaw and pitch.
namespace QcMouseLook {

// Sensitivity is in degrees per point of motion.
QcPlayerInput apply(QcPlayerInput intent, QPointF delta, double sensitivity, bool invert);

} // namespace QcMouseLook
