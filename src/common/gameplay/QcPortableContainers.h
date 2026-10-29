// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcItemStack.h"

#include <QList>

class QcContentRegistry;

// The rules of portable containers (a backpack, decision E-832): a block whose container travels
// with it as the data of its item (QcItemData::contents). Nothing that carries contents goes into
// one, so neither a backpack nor a future shulker box nests, whatever moves the items (clicks,
// shift-clicks, hoppers, placing a filled item).
namespace QcPortableContainers {

// Whether the item places a portable container (QcItemDefinition::portableContainer).
[[nodiscard]] bool isPortable(const QcContentRegistry &content, QcItemId item);
// Whether `stack` may lie in a portable container: empty, or neither a portable container nor
// anything else with contents.
[[nodiscard]] bool mayHold(const QcContentRegistry &content, const QcItemStack &stack);
// Whether every stack of `stacks` may.
[[nodiscard]] bool mayHoldAll(const QcContentRegistry &content, const QList<QcItemStack> &stacks);
// The slots of a placed container for the contents of `stack`: its contents, empty slots after
// them up to `slotCount`; what does not fit the slots or must not lie in it is left out (see
// leftOver).
[[nodiscard]] QList<QcItemStack> slotsOf(const QcContentRegistry &content, const QcItemStack &stack, int slotCount);
// The stacks of `stack`'s contents that slotsOf leaves out, so the caller can drop them.
[[nodiscard]] QList<QcItemStack> leftOver(const QcContentRegistry &content, const QcItemStack &stack, int slotCount);

} // namespace QcPortableContainers
