// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcHanging.h"
#include "common/world/QcBlockAccess.h"

#include <QRandomGenerator>

#include <optional>

class QcHangingManager;

// Where item frames and paintings may hang (decisions E-790, E-791): on solid blocks, in cells
// without collision or liquid, not over another frame or painting on the same face.
namespace QcHangingRules {

// Whether `hanging` holds where it is: every cell free and every block behind it solid; nothing
// while one of these blocks is not loaded.
[[nodiscard]] std::optional<bool> holds(const QcBlockAccess &world, const QcHanging &hanging);
// Whether `hanging` may be placed: it holds and no other frame or painting covers one of its cells
// on its face.
bool fits(const QcBlockAccess &world, const QcHangingManager &hangings, const QcHanging &hanging);
// Whether another frame or painting covers one of the cells of `hanging` on its face.
bool overlaps(const QcHangingManager &hangings, const QcHanging &hanging);
// A painting placed against `face` of a block, in front of it in `cell`: a random one of the
// largest motifs of `definition` that fit, centred on `cell` (a motif of even width or height
// reaches one block further to the right or up); nothing when none fits.
[[nodiscard]] std::optional<QcHanging> painting(const QcBlockAccess &world, const QcHangingManager &hangings,
                                                const QcHangingDefinition &definition, const QcHanging &placed, QRandomGenerator &random);

} // namespace QcHangingRules
