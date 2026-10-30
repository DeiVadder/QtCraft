// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcItemStack.h"

#include <QList>

class QcContentRegistry;

/*! \brief The slots of a screen as one list (inventory, crafting grid, an open machine or chest,
    armour), as the server moves items between them. */
struct QcSlotList
{
    /*! The slots. */
    QList<QcItemStack> stacks;

    /*! Moves up to \a count items (0 = the whole stack) from slot \a from to slot \a to: into an
        empty or matching slot (up to the stack limit of \a content), or swaps whole stacks
        otherwise. Returns false for invalid indices. */
    [[nodiscard]] bool moveBetween(int from, int to, int count, const QcContentRegistry &content);

    /*! Moves as much of the stack in \a from as fits into \a targets: first onto equal stacks
        (in the order of \a targets), then into the first empty ones, up to the stack limits of
        \a content. Returns the number of items moved. */
    int moveInto(int from, const QList<int> &targets, const QcContentRegistry &content);
};
