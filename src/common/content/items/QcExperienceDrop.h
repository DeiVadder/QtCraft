// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/*! \brief Experience points something gives as orbs (decision E-398): a mined ore, a killed
    creature, a pair of bred animals; between \c {min} and \c {max} points, chosen evenly.

    Server side only. */
struct QcExperienceDrop
{
    /*! Fewest points it gives. */
    int min = 0;
    /*! Most points it gives; 0 or less gives none. */
    int max = 0;

    /*! Returns whether it gives no points (\c {max} is 0 or less). */
    bool isEmpty() const { return max <= 0; }
    bool operator==(const QcExperienceDrop &) const = default;
};
