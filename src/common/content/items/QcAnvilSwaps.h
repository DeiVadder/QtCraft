// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcAnvilSwap.h"

#include <QHash>
#include <QString>
#include <QStringList>

class QcContentRegistry;

// Resolves the anvil swaps of all blocks once all packs are loaded (decision E-870): a block's
// `anvilSwap` gives its item the swap into `into` and the item `into` the swap back, both paid with
// one `with`. A swap whose block has no item, that turns a block into itself, names an unknown item
// or an item that already swaps with another one is left out with a warning; the same pair
// declared on both blocks counts once.
namespace QcAnvilSwaps {

struct Resolved
{
    // Item name -> its swap.
    QHash<QString, QcAnvilSwap> byItem;
    QStringList warnings;
};

[[nodiscard]] Resolved resolve(const QcContentRegistry &content);

} // namespace QcAnvilSwaps
