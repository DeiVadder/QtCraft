// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcAnvilSwap.h"

#include <QHash>
#include <QString>
#include <QStringList>

class QcContentRegistry;

/*! \brief Resolves the anvil swaps of all blocks once all packs are loaded (decision E-870): a
    block's \c {anvilSwap} gives its item the swap into \c {into} and the item \c {into} the swap
    back, both paid with one \c {with}.

    A swap whose block has no item, that turns a block into itself, names an unknown item or an
    item that already swaps with another one is left out with a warning; the same pair declared
    on both blocks counts once. */
namespace QcAnvilSwaps {

/*! \brief The result of resolve(): the accepted swaps and the reasons for the left-out ones. */
struct Resolved
{
    /*! Item name -> its swap. */
    QHash<QString, QcAnvilSwap> byItem;
    /*! One line per left-out swap, naming the block, the target item and the reason. */
    QStringList warnings;
};

/*! Goes through all blocks of \a content in order and returns the swaps of those with an
    \c {anvilSwap}, each accepted swap entered for both items. */
[[nodiscard]] Resolved resolve(const QcContentRegistry &content);

} // namespace QcAnvilSwaps
