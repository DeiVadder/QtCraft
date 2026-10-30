// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/villages/QcVillageStyles.h"

#include <functional>
#include <memory>

/*! \brief The fields of a village (decision E-406): the crop cells of a piece (blocks with
    \c {crop}) are planted from the style's crops, one crop per row (a run of crop cells along x in
    one layer), each cell ripe with ripeChance and otherwise at a random earlier stage.

    Draws come from the caller's sequence, so a field is a pure function of the seed like the
    rest of the plan. */
namespace QcVillageFields {

/*! The chance of a planted crop cell to be ripe (its last stage). */
inline constexpr double ripeChance = 0.5;

/*! Returns whether \a structure has crop cells (blocks that are crops in \a content). */
bool hasCrops(const QcStructure &structure, const QcContentRegistry &content);
/*! Returns the crop cells of \a structure (local cells; blocks that are crops in \a content),
    x fastest, then z, then y. */
QList<QcBlockPos> cropCells(const QcStructure &structure, const QcContentRegistry &content);

/*! Returns a copy of \a structure with its crop cells (crops in \a content) planted from
    \a crops (not empty) by weight; \a random yields values in [0, 1). */
[[nodiscard]] std::shared_ptr<const QcStructure> plant(const QcStructure &structure, const QList<QcVillageStyle::Crop> &crops,
                                                       const QcContentRegistry &content, const std::function<double()> &random);

} // namespace QcVillageFields
