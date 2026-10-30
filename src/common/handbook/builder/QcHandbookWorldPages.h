// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/handbook/QcHandbookEntry.h"

#include <QList>

struct QcHandbookContext;

/*! \brief Handbook pages of the world generation, in this order: the biomes (category "biomes"),
    then the terrain overview, the ore veins and the features (category "worldgen"), each in
    content order. */
namespace QcHandbookWorldPages {

/*! Returns the pages, built with the shared parts of \a context. */
QList<QcHandbookEntry> build(const QcHandbookContext &context);

} // namespace QcHandbookWorldPages
