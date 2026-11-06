// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/terrain/QcTreeShape.h"

class QcTreeLayout;

/*! \brief The caps of huge fungi (decision E-1045): a dome and a canopy, both ragged, with their
    glowing blocks and hanging plants, and the small buttresses at the foot of the stem. */
namespace QcFungusCrown {

/*! The farthest a cap reaches sideways from the stem. */
inline constexpr int reach = 6;

/*! Builds a hollow, lumpy dome over the stem of \a params at \a base, \a stemHeight tall, its
    rim ragged by \a seed, with plants hanging from the rim, into \a layout. */
void buildDome(const QcTreeShape::Params &params, QcBlockPos base, int stemHeight, quint64 seed, QcTreeLayout &layout);
/*! Builds a flat, wavy canopy with a drooping rim, and on a tall stem a shelf below it: the stem
    of \a params at \a base, \a stemHeight tall, the rim ragged by \a seed, into \a layout. */
void buildCanopy(const QcTreeShape::Params &params, QcBlockPos base, int stemHeight, quint64 seed, QcTreeLayout &layout);

} // namespace QcFungusCrown
