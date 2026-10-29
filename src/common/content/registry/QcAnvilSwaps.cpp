// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/registry/QcAnvilSwaps.h"

#include "common/content/registry/QcContentRegistry.h"

using namespace Qt::StringLiterals;

namespace QcAnvilSwaps {

namespace {

// Why the swap of `block` cannot be, or empty; `known` holds the swaps accepted so far.
QString problemOf(const QcContentRegistry &content, const QString &block, const QcAnvilSwap &swap, const QHash<QString, QcAnvilSwap> &known)
{
    if (!content.itemId(block)) {
        return u"the block has no item"_s;
    }
    if (swap.into == block) {
        return u"turns the block into itself"_s;
    }
    for (const QString &name : {swap.into, swap.with}) {
        if (!content.itemId(name)) {
            return u"names the unknown item '%1'"_s.arg(name);
        }
    }
    for (const QString &name : {block, swap.into}) {
        const auto present = known.constFind(name);
        if (present != known.constEnd()) {
            return u"'%1' already swaps into '%2'"_s.arg(name, present->into);
        }
    }
    return {};
}

} // namespace

Resolved resolve(const QcContentRegistry &content)
{
    Resolved resolved;
    for (const QcBlockDefinition &block : content.blocks()) {
        if (!block.anvilSwap) {
            continue;
        }
        const QcAnvilSwap &swap = *block.anvilSwap;
        // The other block may declare the same pair back; it counts once.
        if (resolved.byItem.value(block.name) == swap) {
            continue;
        }
        const QString problem = problemOf(content, block.name, swap, resolved.byItem);
        if (!problem.isEmpty()) {
            resolved.warnings.append(u"block '%1': anvil swap into '%2': %3; it has none"_s.arg(block.name, swap.into, problem));
            continue;
        }
        resolved.byItem.insert(block.name, swap);
        resolved.byItem.insert(swap.into, QcAnvilSwap{block.name, swap.with});
    }
    return resolved;
}

} // namespace QcAnvilSwaps
