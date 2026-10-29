// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcBlockDefinition.h"

#include <QDataStream>

#include <optional>

// Wire format of one block definition in the content sync, with its resolved shape. Reading
// validates everything that later feeds physics, light and the mesher.
namespace QcBlockSerializer {

void writeBlock(QDataStream &stream, const QcBlockDefinition &block, const QcShapeDefinition &shape);
// The block read, which carries the read shape inline; std::nullopt (and possibly a corrupt stream) on bad data.
[[nodiscard]] std::optional<QcBlockDefinition> readBlock(QDataStream &stream);
// The fuel time range the parser allows.
bool plausibleFuel(double seconds);

} // namespace QcBlockSerializer
