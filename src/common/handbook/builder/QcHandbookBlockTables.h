// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/handbook/QcHandbookEntry.h"

#include <QList>

class QcContentRegistry;
struct QcBlockDefinition;
struct QcHandbookContext;

/*! \brief Handbook tables built from the block data (category "tables", after the tables of
    QcHandbookOverviewPages, in this order): the Signal Dust components with what they take and
    give, the blocks that are no opaque full cubes with their light and spawning behaviour, the
    light sources and the blast resistance of every block (decision E-685). */
namespace QcHandbookBlockTables {

/*! \brief How much light a block takes away.

    Partial shapes block light only at the sides they cover fully (\c {coversSides}, decision
    E-104) or not at all, whatever their own value says. */
struct LightBlocking
{
    /*! Whether a state of the block blocks light at some of its faces. */
    bool coversSides = false;
    /*! Light levels taken away when passing through, 0..15 (15 blocks all light). */
    int opacity = 15;
};

/*! Returns how much light \a block of \a content takes away: its opacity for a full cube, 0 when
    any of its states has a partial shape. */
LightBlocking lightBlocking(const QcContentRegistry &content, const QcBlockDefinition &block);

/*! Returns the table pages, built with the shared parts of \a context. */
QList<QcHandbookEntry> build(const QcHandbookContext &context);

} // namespace QcHandbookBlockTables
