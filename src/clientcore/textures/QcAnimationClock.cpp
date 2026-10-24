#include "clientcore/textures/QcAnimationClock.h"

#include "common/physics/QcPhysics.h"

#include <algorithm>

void QcAnimationClock::advance(double seconds, double speed)
{
    m_ticks += std::max(seconds, 0.0) * QcPhysics::tickRate * std::max(speed, 0.0);
}
