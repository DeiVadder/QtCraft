// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/physics/QcPlayerInput.h"

#include <QPointF>

/*! \brief Turns relative mouse motion (in points, y down) into view yaw and pitch. */
namespace QcMouseLook {

/*! Returns \a intent turned by the mouse motion \a delta: \a sensitivity in degrees per point of
    motion, \a invert turns looking up and down around. */
QcPlayerInput apply(QcPlayerInput intent, QPointF delta, double sensitivity, bool invert);

} // namespace QcMouseLook
