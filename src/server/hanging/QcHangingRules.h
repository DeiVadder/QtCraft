// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcHanging.h"
#include "common/world/QcBlockAccess.h"

#include <QRandomGenerator>

#include <optional>

class QcHangingManager;

/*! \brief Where item frames and paintings may hang (decisions E-790, E-791): on solid blocks, in
    cells without collision or liquid, not over another frame or painting on the same face. */
namespace QcHangingRules {

/*! Returns whether \a hanging holds where it is in \a world: every cell free and every block
    behind it solid; nothing while one of these blocks is not loaded. */
[[nodiscard]] std::optional<bool> holds(const QcBlockAccess &world, const QcHanging &hanging);
/*! Returns whether \a hanging may be placed in \a world among \a hangings: it holds and no other
    frame or painting covers one of its cells on its face. */
bool fits(const QcBlockAccess &world, const QcHangingManager &hangings, const QcHanging &hanging);
/*! Returns whether another frame or painting of \a hangings covers one of the cells of
    \a hanging on its face. */
bool overlaps(const QcHangingManager &hangings, const QcHanging &hanging);
/*! Returns the painting \a placed against \c {face} of a block, in front of it in \c {cell}: a
    random one (drawn from \a random) of the largest motifs of \a definition that fit in \a world
    among \a hangings, centred on \c {cell} (a motif of even width or height reaches one block
    further to the right or up); nothing when none fits. */
[[nodiscard]] std::optional<QcHanging> painting(const QcBlockAccess &world, const QcHangingManager &hangings,
                                                const QcHangingDefinition &definition, const QcHanging &placed, QRandomGenerator &random);

} // namespace QcHangingRules
