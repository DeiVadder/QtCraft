// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcHoldDisplay.h"

#include <QDataStream>

#include <optional>

// How items are held (decision E-831) in the content sync (QcContentSerializer): the `hold` of items
// and blocks and the category defaults, read back within the parser's limits (QcHoldParser).
namespace QcHoldSerializer {

void write(QDataStream &stream, const QcHoldDisplay &display);
// std::nullopt when the data is corrupt or out of the parser's limits.
[[nodiscard]] std::optional<QcHoldDisplay> read(QDataStream &stream);

void writeDefaults(QDataStream &stream, const QcHoldDefaults &defaults);
[[nodiscard]] std::optional<QcHoldDefaults> readDefaults(QDataStream &stream);

} // namespace QcHoldSerializer
