#pragma once

#include "common/physics/QcPlayerInput.h"

#include <QPointF>

// Turns relative mouse motion (in points, y down) into view yaw and pitch.
namespace QcMouseLook {

// Sensitivity is in degrees per point of motion.
QcPlayerInput apply(QcPlayerInput intent, QPointF delta, double sensitivity, bool invert);

} // namespace QcMouseLook
