// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcArmourDefinition.h"

#include <QDataStream>

#include <optional>

/*! \brief The armour of an item in the content sync (QcContentSerializer): written by the
    server, read and checked by the client like the rest of the content. */
namespace QcArmourSerializer {

/*! Writes \a armour to \a stream: a presence flag, then the definition when there is one. */
void write(QDataStream &stream, const std::optional<QcArmourDefinition> &armour);
/*! Returns the armour of one item read from \a stream: an engaged optional holding std::nullopt
    for an item without armour, std::nullopt when the data is corrupt or out of the parser's
    limits. */
[[nodiscard]] std::optional<std::optional<QcArmourDefinition>> read(QDataStream &stream);

} // namespace QcArmourSerializer
