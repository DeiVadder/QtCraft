// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/input/QcHotbarRadial.h"

#include <QPointF>

#include <optional>

/*! \brief The hotbar's radial menu with the mouse (decision E-614): while its key is held the mouse
    does not turn the view but moves a pointer from the centre, and the direction of the pointer
    picks the slot like the pad's right stick (QcHotbarRadial).

    The pointer stays within \c {reach} pixels of the centre, so turning back needs no more than
    that; the menu points at a slot once the pointer is half of \c {reach} out. State only,
    without a session or a window. */
class QcPointerRadial
{
public:
    /*! Mouse travel (pixels of the window, before the sensitivity) for a full deflection. */
    static constexpr double reach = 120.0;

    explicit QcPointerRadial(int slotCount);

    /*! Returns whether the menu is open. */
    bool isOpen() const { return m_radial.isOpen(); }
    /*! Returns the highlighted slot while open (-1 while closed). */
    int highlighted() const { return m_radial.highlighted(); }
    /*! Returns the pointer's offset from the centre, in pixels. */
    QPointF pointer() const { return m_pointer; }

    /*! Opens on the slot in the hand, \a selectedSlot, with the pointer in the centre. */
    void open(int selectedSlot);
    /*! The mouse moved by \a delta (y positive down). Returns whether the highlighted slot changed.
     */
    bool move(QPointF delta);
    /*! Closes and returns the slot to select (nullopt when it was closed already). */
    std::optional<int> close();
    /*! Closes without selecting. */
    void cancel();

private:
    QcHotbarRadial m_radial;
    QPointF m_pointer;
};
