// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcItemStack.h"

#include <QList>

class QcContentRegistry;

// The slots of a screen as one list (inventory, crafting grid, an open machine or chest, armour), as
// the server moves items between them.
struct QcSlotList
{
    QList<QcItemStack> stacks;

    // Moves up to `count` items (0 = the whole stack) between two slots: into an empty or matching
    // slot, or swaps whole stacks otherwise. Returns false for invalid indices.
    [[nodiscard]] bool moveBetween(int from, int to, int count, const QcContentRegistry &content);

    // Moves as much of the stack in `from` as fits into `targets`: first onto equal stacks (in the
    // order of `targets`), then into the first empty ones. Returns the number of items moved.
    int moveInto(int from, const QList<int> &targets, const QcContentRegistry &content);
};
