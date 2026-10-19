#pragma once

#include "common/content/definitions/QcArmourDefinition.h"

#include <QDataStream>

#include <optional>

// The armour of an item in the content sync (QcContentSerializer): written by the server, read
// and checked by the client like the rest of the content.
namespace QcArmourSerializer {

void write(QDataStream &stream, const std::optional<QcArmourDefinition> &armour);
// The armour of one item: an engaged optional holding std::nullopt for an item without armour,
// std::nullopt when the data is corrupt or out of the parser's limits.
[[nodiscard]] std::optional<std::optional<QcArmourDefinition>> read(QDataStream &stream);

} // namespace QcArmourSerializer
