// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Experience points something gives as orbs (decision E-398): a mined ore, a killed creature, a pair
// of bred animals; between `min` and `max` points, chosen evenly. Server side only.
struct QcExperienceDrop
{
    int min = 0;
    int max = 0;

    bool isEmpty() const { return max <= 0; }
    bool operator==(const QcExperienceDrop &) const = default;
};
