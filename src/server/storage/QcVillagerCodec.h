#pragma once

#include "QcTradeDefinitions.h"
#include "QcVillager.h"

#include <QDataStream>

#include <optional>

// A villager's record in the entity data of its chunk (decisions E-411, E-414; world format 12): its
// home, workstation, profession, level, experience, the uses and demand of its offers and its last
// restock.
namespace QcVillagerCodec {

void write(QDataStream &stream, const QcVillagerRecord &villager);
// std::nullopt for damaged data (a stream error, numbers out of range, a place outside the world).
[[nodiscard]] std::optional<QcVillagerRecord> read(QDataStream &stream);

} // namespace QcVillagerCodec
