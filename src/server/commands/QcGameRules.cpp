// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/commands/QcGameRules.h"

#include <QCoreApplication>

#include <algorithm>
#include <array>

namespace QcGameRules {

namespace {

constexpr const char *players = QT_TRANSLATE_NOOP("QcGameRules", "Players");
constexpr const char *world = QT_TRANSLATE_NOOP("QcGameRules", "World");
constexpr const char *creatures = QT_TRANSLATE_NOOP("QcGameRules", "Creatures");
constexpr const char *blocks = QT_TRANSLATE_NOOP("QcGameRules", "Blocks");

// keepInventory; daylightCycle and mobSpawning (decision E-221); randomTicks (E-295);
// quasiConnectivity (E-338); fireSpread (E-392); weatherCycle (E-395); mobGriefing (E-460);
// explosionBlockDamage (E-603); spawnChunkRadius (E-377); maxEntityCramming (E-401); the
// invulnerability ticks (E-429); announceAdvancements (E-924).
constexpr std::array<Rule, 14> rules{{
    {u"keepInventory", players, QT_TRANSLATE_NOOP("QcGameRules", "Players keep their inventory when they die."), &QcWorldInfo::keepInventory,
     nullptr, 1, 1},
    {u"announceAdvancements", players, QT_TRANSLATE_NOOP("QcGameRules", "Other players read in the chat when someone reaches an advancement."),
     &QcWorldInfo::announceAdvancements, nullptr, 1, 1},
    {u"playerInvulnerabilityTicks", players, QT_TRANSLATE_NOOP("QcGameRules", "Ticks a player stays invulnerable after a hurt (0: every hit counts)."),
     nullptr, &QcWorldInfo::playerInvulnerabilityTicks, QcWorldInfo::maxInvulnerabilityTicks, QcWorldInfo::defaultInvulnerabilityTicks},
    {u"daylightCycle", world, QT_TRANSLATE_NOOP("QcGameRules", "The time of day runs."), &QcWorldInfo::daylightCycle, nullptr, 1, 1},
    {u"weatherCycle", world, QT_TRANSLATE_NOOP("QcGameRules", "The weather changes by itself."), &QcWorldInfo::weatherCycle, nullptr, 1, 1},
    {u"spawnChunkRadius", world, QT_TRANSLATE_NOOP("QcGameRules", "Chunks around the spawn that stay loaded (0: none, 1: the spawn chunk, 2: 3 x 3 …)."),
     nullptr, &QcWorldInfo::spawnChunkRadius, QcWorldInfo::maxSpawnChunkRadius, 0},
    {u"mobSpawning", creatures, QT_TRANSLATE_NOOP("QcGameRules", "Creatures spawn by themselves."), &QcWorldInfo::mobSpawning, nullptr, 1, 1},
    {u"mobGriefing", creatures, QT_TRANSLATE_NOOP("QcGameRules", "Creatures change blocks (shadewalkers take and place blocks)."), &QcWorldInfo::mobGriefing,
     nullptr, 1, 1},
    {u"maxEntityCramming", creatures, QT_TRANSLATE_NOOP("QcGameRules", "Creatures in one block cell beyond which they take damage (0: never)."), nullptr,
     &QcWorldInfo::maxEntityCramming, QcWorldInfo::maxEntityCrammingLimit, 24},
    {u"mobInvulnerabilityTicks", creatures,
     QT_TRANSLATE_NOOP("QcGameRules", "Ticks a creature stays invulnerable after a hurt (0: every hit counts)."), nullptr,
     &QcWorldInfo::mobInvulnerabilityTicks, QcWorldInfo::maxInvulnerabilityTicks, QcWorldInfo::defaultInvulnerabilityTicks},
    {u"randomTicks", blocks, QT_TRANSLATE_NOOP("QcGameRules", "Blocks get random ticks: plants and crops grow, soil changes."), &QcWorldInfo::randomTicks,
     nullptr, 1, 1},
    {u"fireSpread", blocks, QT_TRANSLATE_NOOP("QcGameRules", "Fire spreads to flammable blocks and burns them away."), &QcWorldInfo::fireSpread, nullptr,
     1, 1},
    {u"quasiConnectivity", blocks, QT_TRANSLATE_NOOP("QcGameRules", "Pistons are also powered through the block above them."),
     &QcWorldInfo::quasiConnectivity, nullptr, 1, 1},
    {u"explosionBlockDamage", blocks, QT_TRANSLATE_NOOP("QcGameRules", "Explosions break, light and blow away blocks."),
     &QcWorldInfo::explosionBlockDamage, nullptr, 1, 1},
}};

} // namespace

std::span<const Rule> all()
{
    return rules;
}

const Rule *find(QStringView name)
{
    const auto found = std::ranges::find(rules, name, &Rule::name);
    return found == rules.end() ? nullptr : &*found;
}

qint64 value(const Rule &rule, const QcWorldInfo &info)
{
    return rule.flag ? static_cast<qint64>(info.*(rule.flag)) : static_cast<qint64>(info.*(rule.number));
}

} // namespace QcGameRules
