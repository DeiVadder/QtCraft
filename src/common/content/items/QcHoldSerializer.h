// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcHoldDisplay.h"

#include <QDataStream>

#include <optional>

/*! \brief How items are held (decision E-831) in the content sync (QcContentSerializer): the
    \c {hold} of items and blocks and the category defaults, read back within the parser's limits
    (QcHoldParser). */
namespace QcHoldSerializer {

/*! Writes \a display to \a stream: category and grip codes, then the three transforms. */
void write(QDataStream &stream, const QcHoldDisplay &display);
/*! Returns the display written by write() read from \a stream; std::nullopt when the data is
    corrupt or out of the parser's limits. */
[[nodiscard]] std::optional<QcHoldDisplay> read(QDataStream &stream);

/*! Writes the display of every category of \a defaults to \a stream, in category order. */
void writeDefaults(QDataStream &stream, const QcHoldDefaults &defaults);
/*! Returns the defaults written by writeDefaults() read from \a stream; std::nullopt when one
    category's display is corrupt. */
[[nodiscard]] std::optional<QcHoldDefaults> readDefaults(QDataStream &stream);

} // namespace QcHoldSerializer
