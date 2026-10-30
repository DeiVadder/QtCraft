// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/showcase/QcShowcaseArea.h"

/*! \brief Every village style of the registry (decision E-403), mod styles included: one row per
    style with its well, its lamp post and each of its buildings once, doors to a road in front,
    each labelled with its title and style. */
namespace QcShowcaseVillageArea {

/*! Returns whether \a content has a village style to show. */
bool hasStyles(const QcContentRegistry &content);
/*! Returns the area built for \a context with its south west corner on the ground at
    \a anchor (QcShowcaseAreaBuilder). */
QcShowcaseArea build(const QcShowcaseContext &context, QcBlockPos anchor);

} // namespace QcShowcaseVillageArea
