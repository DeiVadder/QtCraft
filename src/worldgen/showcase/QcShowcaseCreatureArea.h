// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/showcase/QcShowcaseArea.h"

/*! \brief A glass enclosure for every creature of the registry, side by side (decision E-220):
    sized to the creature, roofed (with the podium's block for creatures that burn in daylight,
    decision E-426), with walls for hostile ones thicker than their reach (attackRange) beyond the
    glass, and a label.

    The creatures are spawned once with the world and stay (persistent and confined: one that
    teleports or takes blocks stays in its pen, decision E-460). */
namespace QcShowcaseCreatureArea {

/*! Returns the area built for \a context with its south west corner on the ground at
    \a anchor (QcShowcaseAreaBuilder). */
QcShowcaseArea build(const QcShowcaseContext &context, QcBlockPos anchor);

} // namespace QcShowcaseCreatureArea
