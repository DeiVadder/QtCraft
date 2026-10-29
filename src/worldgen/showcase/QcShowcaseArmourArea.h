// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/showcase/QcShowcaseArea.h"

#include <QStringList>

// Every armour set of the registry on an armour stand (decision E-238), mod sets included, weakest
// first; each stand stands on a podium facing the visitors, with the set's name and armour points,
// and holds a bow, a pickaxe or a sword in turn (held items, decision E-831).
// Without armour in the content the area is empty (a single podium-less row).
namespace QcShowcaseArmourArea {

// The sets in the order the area shows them: by armour points, then by name.
QStringList sets(const QcContentRegistry &content);

QcShowcaseArea build(const QcShowcaseContext &context, QcBlockPos anchor);

} // namespace QcShowcaseArmourArea
