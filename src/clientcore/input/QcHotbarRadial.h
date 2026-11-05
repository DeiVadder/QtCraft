// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>

/*! \brief The radial menu of the hotbar on a pad (decision E-581): held open by a button, the
    hotbar's slots sit on a circle, the right stick points at one and letting go of the button
    selects it.

    Slot 0 is at the top and the others follow clockwise. It opens on the slot in the hand, so
    letting go without pointing keeps it. State only, without a session or a source. */
class QcHotbarRadial
{
public:
    /*! A stick pushed less far than this (raw) points at nothing: the last slot stays
        highlighted. */
    static constexpr float pointDeflection = 0.5f;

    explicit QcHotbarRadial(int slots);

    /*! Returns whether the menu is open. */
    bool isOpen() const { return m_open; }
    /*! Returns the highlighted slot while open (-1 while closed). */
    int highlighted() const { return m_open ? m_highlighted : -1; }

    /*! Opens the menu on \a selectedSlot, the slot in the hand. */
    void open(int selectedSlot);
    /*! The stick points at (\a x, \a y), y positive down. Returns whether the highlighted slot
        changed. */
    bool point(float x, float y);
    /*! Closes and returns the slot to select (nullopt when it was closed already). */
    std::optional<int> close();
    /*! Closes without selecting (a screen opened, the pad stopped leading). */
    void cancel() { m_open = false; }

    /*! Returns the slot the direction (\a x, \a y) points at among \c {slots} slots (slot 0 at
        the top, clockwise); nullopt inside pointDeflection. */
    static std::optional<int> slotAt(float x, float y, int slots);

private:
    int m_slots = 1;
    int m_highlighted = 0;
    bool m_open = false;
};
