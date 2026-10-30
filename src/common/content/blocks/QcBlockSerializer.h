// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"

#include <QDataStream>

#include <optional>

/*! \brief Wire format of one block definition in the content sync, with its resolved shape.

    Reading validates everything that later feeds physics, light and the mesher. */
namespace QcBlockSerializer {

/*! Writes the parts of \a block that reach the client to \a stream, with \a shape in place of
    its own shape. */
void writeBlock(QDataStream &stream, const QcBlockDefinition &block, const QcShapeDefinition &shape);
/*! Returns the block read from \a stream, which carries the read shape inline; std::nullopt (and
    possibly a corrupt stream) on bad data. */
[[nodiscard]] std::optional<QcBlockDefinition> readBlock(QDataStream &stream);
/*! Returns whether \a seconds is a fuel time the parser allows (finite, 0 to 100000). */
bool plausibleFuel(double seconds);

} // namespace QcBlockSerializer
