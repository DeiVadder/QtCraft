// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/registry/QcSpawnWeights.h"

#include "common/content/registry/QcContentRegistry.h"

#include <algorithm>

using namespace Qt::StringLiterals;

QcSpawnWeights::QcSpawnWeights(const QcContentRegistry &content)
{
    m_tables.insert(QString(), tableOf(content, QString()));
    for (const QcBiomeDefinition &biome : content.worldgen().biomes()) {
        m_tables.insert(biome.name, tableOf(content, biome.name));
    }
}

double QcSpawnWeights::ruleWeight(const QcMobDefinition &mob, const QString &biome)
{
    double weight = 0.0;
    const QList<QcMobDefinition::SpawnRule> rules = mob.spawnRules();
    for (const QcMobDefinition::SpawnRule &rule : rules) {
        if (rule.biomes.isEmpty() || rule.biomes.contains(biome)) {
            weight = std::max(weight, rule.weight);
        }
    }
    return weight;
}

QcSpawnWeights::Table QcSpawnWeights::tableOf(const QcContentRegistry &content, const QString &biome)
{
    const auto definition = std::ranges::find(content.worldgen().biomes(), biome, &QcBiomeDefinition::name);
    Table table;
    for (const QcMobDefinition &mob : content.mobs()) {
        double weight = mob.isMonster() ? ruleWeight(mob, biome) : 0.0;
        // The biome's own weight counts only for kinds it has at all.
        if (weight > 0.0 && definition != content.worldgen().biomes().end()) {
            const auto own = std::ranges::find(definition->monsterWeights, mob.name, &QcSpawnWeight::mob);
            weight = own != definition->monsterWeights.end() ? own->weight : weight;
        }
        table.weights.append(weight);
        table.total += weight;
    }
    return table;
}

const QcSpawnWeights::Table &QcSpawnWeights::find(const QString &biome) const
{
    // A biome no pack has (a generator of its own) lists the kinds as a world without biomes.
    const auto table = m_tables.constFind(biome);
    return table != m_tables.cend() ? *table : *m_tables.constFind(QString());
}

double QcSpawnWeights::weight(int type, const QString &biome) const
{
    const Table &table = find(biome);
    return type >= 0 && type < table.weights.size() ? table.weights.at(type) : 0.0;
}

double QcSpawnWeights::total(const QString &biome) const
{
    return find(biome).total;
}

QStringList QcSpawnWeights::problems(const QcContentRegistry &content)
{
    QStringList warnings;
    for (const QcBiomeDefinition &biome : content.worldgen().biomes()) {
        for (const QcSpawnWeight &entry : biome.monsterWeights) {
            const std::optional<int> type = content.mobIndex(entry.mob);
            if (!type) {
                warnings.append(u"biome '%1': unknown creature '%2' in 'monsters', left out"_s.arg(biome.name, entry.mob));
            } else if (!content.mobs()[*type].isMonster()) {
                warnings.append(u"biome '%1': '%2' in 'monsters' is no monster, left out"_s.arg(biome.name, entry.mob));
            }
        }
    }
    return warnings;
}
