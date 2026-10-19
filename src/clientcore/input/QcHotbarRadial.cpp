#include "clientcore/input/QcHotbarRadial.h"

#include <algorithm>
#include <cmath>
#include <numbers>

QcHotbarRadial::QcHotbarRadial(int slots)
    : m_slots(std::max(1, slots))
{
}

void QcHotbarRadial::open(int selectedSlot)
{
    m_open = true;
    m_highlighted = std::clamp(selectedSlot, 0, m_slots - 1);
}

bool QcHotbarRadial::point(float x, float y)
{
    if (!m_open) {
        return false;
    }

    const std::optional<int> slot = slotAt(x, y, m_slots);
    if (!slot || *slot == m_highlighted) {
        return false;
    }
    m_highlighted = *slot;
    return true;
}

std::optional<int> QcHotbarRadial::close()
{
    if (!m_open) {
        return std::nullopt;
    }
    m_open = false;
    return m_highlighted;
}

std::optional<int> QcHotbarRadial::slotAt(float x, float y, int slots)
{
    if (!std::isfinite(x) || !std::isfinite(y) || std::hypot(x, y) < pointDeflection || slots < 1) {
        return std::nullopt;
    }

    // Angle clockwise from straight up (y is positive down), in [0, 2 pi).
    double angle = std::atan2(double(x), double(-y));
    if (angle < 0.0) {
        angle += 2.0 * std::numbers::pi;
    }
    const double sector = 2.0 * std::numbers::pi / slots;
    // Each slot's sector is centred on its direction.
    return int(std::floor(angle / sector + 0.5)) % slots;
}
