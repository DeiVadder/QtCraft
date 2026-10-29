// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcMobDefinition.h"

#include <QDataStream>

#include <optional>

// Wire format of one creature definition in the content sync: what the client shows and uses of it
// (size, model parts, sounds, glow, breeding food, tameable, milking, the thread of a family).
// Reading validates everything that later feeds physics and the mesher.
namespace QcMobSerializer {

void writeMob(QDataStream &stream, const QcMobDefinition &mob);
// The creature read; std::nullopt (and possibly a corrupt stream) on bad data.
[[nodiscard]] std::optional<QcMobDefinition> readMob(QDataStream &stream);

} // namespace QcMobSerializer
