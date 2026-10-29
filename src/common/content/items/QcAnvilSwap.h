// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

// An anvil swap (decision E-870): the anvil turns a stack of one item into the same number of
// `into`, using up one `with` from its right slot. A block declares it on one side (block field
// `anvilSwap`); QcAnvilSwaps gives both items the swap, each pointing at the other. Names are
// qualified. Server side only.
struct QcAnvilSwap
{
    QString into;
    QString with;

    bool operator==(const QcAnvilSwap &) const = default;
};
