// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/showcase/QcShowcaseArea.h"

/*! \brief Every item of the registry, one of each, in a row of chests (decision E-220); each
    chest has a label with the range of items it holds. */
namespace QcShowcaseItemArea {

/*! Returns the area built for \a context with its south west corner on the ground at
    \a anchor (QcShowcaseAreaBuilder). */
QcShowcaseArea build(const QcShowcaseContext &context, QcBlockPos anchor);

} // namespace QcShowcaseItemArea
