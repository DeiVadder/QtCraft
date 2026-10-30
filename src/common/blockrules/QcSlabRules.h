// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/blockrules/QcBlockRules.h"

/*! \brief Two slabs in one cell make a full block.

    A slab (state \c {type}: bottom, top, double) doubles up when it is clicked on its open half
    or placed into a slab of the other half. A vertical slab (decision E-755:
    \c {"facing": "against"} and the state \c {double}) stands against one side of its cell and
    doubles up when it is clicked on its open side or placed into a vertical slab facing the
    other way. */
namespace QcSlabRules {

/*! Returns the double slab that placing the slab \a state against \a face of \a clicked in \a world
    makes, std::nullopt when it makes none; \a upper is the half the click aimed at. */
[[nodiscard]] std::optional<QcBlockRules::Placement> doubled(const QcBlockAccess &world, QcBlockId state, QcBlockPos clicked, QcBlockFace face,
                                                             bool upper);

/*! Returns the same for the vertical slab \a state that would face \a facing where it is placed,
    placed against \a face of \a clicked in \a world. */
[[nodiscard]] std::optional<QcBlockRules::Placement> doubledVertical(const QcBlockAccess &world, QcBlockId state, QcBlockPos clicked,
                                                                     QcBlockFace face, const QString &facing);

/*! Returns whether the block of \a definition is a vertical slab. */
bool isVertical(const QcBlockDefinition &definition);

} // namespace QcSlabRules
