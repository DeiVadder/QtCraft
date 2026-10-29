// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/input/QcPointerRadial.h"

#include <cmath>

QcPointerRadial::QcPointerRadial(int slotCount)
    : m_radial(slotCount)
{
}

void QcPointerRadial::open(int selectedSlot)
{
    m_radial.open(selectedSlot);
    m_pointer = {};
}

bool QcPointerRadial::move(QPointF delta)
{
    if (!m_radial.isOpen() || !std::isfinite(delta.x()) || !std::isfinite(delta.y())) {
        return false;
    }

    m_pointer += delta;
    // Kept on the rim: moving the other way turns back at once.
    const double length = std::hypot(m_pointer.x(), m_pointer.y());
    if (length > reach) {
        m_pointer *= reach / length;
    }
    return m_radial.point(static_cast<float>(m_pointer.x() / reach), static_cast<float>(m_pointer.y() / reach));
}

std::optional<int> QcPointerRadial::close()
{
    m_pointer = {};
    return m_radial.close();
}

void QcPointerRadial::cancel()
{
    m_pointer = {};
    m_radial.cancel();
}
