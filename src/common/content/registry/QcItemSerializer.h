// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcItemDefinition.h"

#include <QDataStream>

#include <optional>

// The definition of an item in the content sync (QcContentSerializer): its stack, tool, food and fuel
// values, tags, what it places or fills, its armour (QcArmourSerializer) and how it is used (launcher,
// projectile, throwable, drink, fishing rod). Written by the server, read and checked by the client.
namespace QcItemSerializer {

void write(QDataStream &stream, const QcItemDefinition &item);
// The item read from `stream`; std::nullopt when the data is corrupt or out of the parser's limits.
// The caller registers it (and refuses a duplicate name).
[[nodiscard]] std::optional<QcItemDefinition> read(QDataStream &stream);

} // namespace QcItemSerializer
