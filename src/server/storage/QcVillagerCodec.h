// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcTradeDefinitions.h"
#include "server/villages/QcVillager.h"

#include <QDataStream>

#include <optional>

// A villager's record as the villager component of its creature's entry (decisions E-411, E-414,
// E-420): its village, home, workstation, profession, level, experience, look, the crops it brought
// in (version 1), then the uses and demand of its offers and its last restock (version 2), then
// when it last slept (version 3, decision E-470).
namespace QcVillagerCodec {

inline constexpr quint16 version = 3;

// Writes the fields of the current version.
void write(QDataStream &stream, const QcVillagerRecord &villager);
// The fields of `version` (up to the current one), defaults for those it lacks; std::nullopt for
// damaged data (a stream error, numbers out of range, a place outside the world).
[[nodiscard]] std::optional<QcVillagerRecord> read(QDataStream &stream, quint16 fileVersion);

} // namespace QcVillagerCodec
