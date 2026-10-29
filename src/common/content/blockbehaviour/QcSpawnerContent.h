// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

class QcContentRegistry;
struct QcMobDefinition;

// Checks what spawners, dungeons and loot tables name (decisions E-520..E-522) when the content is
// finished, and which creature kinds a spawner may bring forth at all.
namespace QcSpawnerContent {

// Why creatures of `mob` never come from a spawner, empty when they may: villagers belong to a
// village and are kept with the world, which a spawner's creatures are not; water creatures (fish)
// need water, which a spawner's cells (room for the body, ground below) never are.
QString refusal(const QcMobDefinition &mob);
// The warnings for unknown or refused kinds of the dungeons (those entries are dropped; without any
// left the dungeons are off), of spawner blocks and of structures' spawners (those spawn nothing),
// for unknown loot tables of the dungeons and structures (their chests stay empty) and for unknown
// items of loot tables (they give nothing).
QStringList resolve(QcContentRegistry &content);

} // namespace QcSpawnerContent
