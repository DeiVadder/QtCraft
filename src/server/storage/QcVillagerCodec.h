#pragma once

#include "QcVillager.h"

#include <QDataStream>

#include <optional>

// A villager's record in the entity data of its chunk (decision E-411, world format 11).
namespace QcVillagerCodec {

void write(QDataStream &stream, const QcVillagerRecord &villager);
// std::nullopt for damaged data (a stream error, numbers out of range, a place outside the world).
[[nodiscard]] std::optional<QcVillagerRecord> read(QDataStream &stream);

} // namespace QcVillagerCodec
