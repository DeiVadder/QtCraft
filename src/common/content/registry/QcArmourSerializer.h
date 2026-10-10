#pragma once

#include "QcArmourDefinition.h"

#include <QDataStream>

#include <optional>

// The armour of an item in the content sync (QcContentSerializer): written by the server, read
// and checked by the client like the rest of the content.
namespace QcArmourSerializer {

void write(QDataStream &stream, const std::optional<QcArmourDefinition> &armour);
// False when the data is corrupt or out of the parser's limits.
bool read(QDataStream &stream, std::optional<QcArmourDefinition> &armour);

} // namespace QcArmourSerializer
