#pragma once

#include "clientcore/input/QcHotbarRadial.h"

#include <QPointF>

#include <optional>

// The hotbar's radial menu with the mouse (decision E-614): while its key is held the mouse does
// not turn the view but moves a pointer from the centre, and the direction of the pointer picks the
// slot like the pad's right stick (QcHotbarRadial). The pointer stays within `reach` pixels of
// the centre, so turning back needs no more than that; the menu points at a slot once the pointer
// is half of `reach` out. State only, without a session or a window.
class QcPointerRadial
{
public:
    // Mouse travel (pixels of the window, before the sensitivity) for a full deflection.
    static constexpr double reach = 120.0;

    explicit QcPointerRadial(int slotCount);

    bool isOpen() const { return m_radial.isOpen(); }
    int highlighted() const { return m_radial.highlighted(); }
    QPointF pointer() const { return m_pointer; }

    // Opens on the slot in the hand with the pointer in the centre.
    void open(int selectedSlot);
    // The mouse moved by `delta` (y positive down); whether the highlighted slot changed.
    bool move(QPointF delta);
    // Closes and returns the slot to select (nullopt when it was closed already).
    std::optional<int> close();
    void cancel();

private:
    QcHotbarRadial m_radial;
    QPointF m_pointer;
};
