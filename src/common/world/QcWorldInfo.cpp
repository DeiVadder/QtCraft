#include "common/world/QcWorldInfo.h"

#include <algorithm>

using namespace Qt::StringLiterals;

void QcWorldInfo::readGameRules(const QJsonObject &rules)
{
    keepInventory = rules.value(u"keepInventory"_s).toBool(true);
    daylightCycle = rules.value(u"daylightCycle"_s).toBool(true);
    mobSpawning = rules.value(u"mobSpawning"_s).toBool(true);
    randomTicks = rules.value(u"randomTicks"_s).toBool(true);
    fireSpread = rules.value(u"fireSpread"_s).toBool(true);
    quasiConnectivity = rules.value(u"quasiConnectivity"_s).toBool(true);
    weatherCycle = rules.value(u"weatherCycle"_s).toBool(true);
    mobGriefing = rules.value(u"mobGriefing"_s).toBool(true);
    explosionBlockDamage = rules.value(u"explosionBlockDamage"_s).toBool(true);
    spawnChunkRadius = std::clamp(rules.value(u"spawnChunkRadius"_s).toInt(0), 0, maxSpawnChunkRadius);
    maxEntityCramming = std::clamp(rules.value(u"maxEntityCramming"_s).toInt(24), 0, maxEntityCrammingLimit);
    playerInvulnerabilityTicks = std::clamp(rules.value(u"playerInvulnerabilityTicks"_s).toInt(defaultInvulnerabilityTicks), 0,
                                            maxInvulnerabilityTicks);
    mobInvulnerabilityTicks = std::clamp(rules.value(u"mobInvulnerabilityTicks"_s).toInt(defaultInvulnerabilityTicks), 0,
                                         maxInvulnerabilityTicks);
}
