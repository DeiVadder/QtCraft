// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "content/QcWorldCensus.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/world/QcChunk.h"
#include "content/QcProgressionRules.h"
#include "worldgen/generators/QcWorldGenerator.h"

#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

namespace Node = QcProgressionNode;

// Counts what a sample of chunks of one dimension holds.
class Survey
{
public:
    Survey(const QcWorldGenerator &generator, const QcContentRegistry &content)
        : m_generator(generator)
        , m_content(content)
    {
    }

    void visitChunk(QcChunkPos pos);
    void visitSitesAround(QcBlockPos centre, int reach);
    // The creatures that spawn by themselves on what was found; `namedBiomesOnly` as beyond the
    // overworld, where a kind comes only where it names the biome (decision E-1050).
    void addNaturalSpawns(bool namedBiomesOnly);

    [[nodiscard]] QHash<QString, qint64> found() const { return m_found; }

private:
    void countBlocks(const QcChunk &chunk);
    void countFurnishing(QcChunkPos pos);
    [[nodiscard]] bool spawnsHere(const QcMobDefinition::SpawnRule &rule, bool namedBiomesOnly) const;

    const QcWorldGenerator &m_generator;
    const QcContentRegistry &m_content;
    QHash<QString, qint64> m_found;
    QSet<QString> m_biomes;
};

void Survey::countBlocks(const QcChunk &chunk)
{
    QHash<QcBlockId, qint64> counts;
    for (int y = 0; y < QcWorld::worldHeight; ++y) {
        for (int z = 0; z < QcWorld::chunkSize; ++z) {
            for (int x = 0; x < QcWorld::chunkSize; ++x) {
                ++counts[chunk.block(x, y, z)];
            }
        }
    }
    for (const auto [block, count] : counts.asKeyValueRange()) {
        if (block != qcAirBlockId) {
            m_found[Node::block(m_content.block(block).name)] += count;
        }
    }
}

void Survey::countFurnishing(QcChunkPos pos)
{
    const QcChunkFurnishing furnishing = m_generator.furnishingIn(pos);
    for (const QcChunkFurnishing::Chest &chest : furnishing.chests) {
        ++m_found[Node::chest(chest.lootTable)];
    }
    for (const QcChunkFurnishing::Cart &cart : furnishing.carts) {
        if (!cart.lootTable.isEmpty()) {
            ++m_found[Node::chest(cart.lootTable)];
        }
    }
    for (const QcStructureDefinition::Spawner &spawner : furnishing.spawners) {
        ++m_found[Node::creature(spawner.mob)];
    }
    for (const QcChunkFurnishing::Resident &resident : furnishing.residents) {
        ++m_found[Node::creature(resident.mob)];
    }
}

void Survey::visitChunk(QcChunkPos pos)
{
    countBlocks(*m_generator.generate(pos));
    countFurnishing(pos);
    m_biomes.insert(m_generator.biomeAt(pos.x * QcWorld::chunkSize, pos.z * QcWorld::chunkSize));
    if (m_generator.villageFor(pos)) {
        ++m_found[Node::villagers()];
    }
}

void Survey::visitSitesAround(QcBlockPos centre, int reach)
{
    const QcStructureSites sites = m_generator.structureSitesNear(centre, reach);
    for (const QcSpawnArea &area : sites.spawnAreas) {
        for (const QcStructureDefinition::SpawnArea::Monster &monster : area.monsters) {
            ++m_found[Node::creature(monster.mob)];
        }
    }
}

bool Survey::spawnsHere(const QcMobDefinition::SpawnRule &rule, bool namedBiomesOnly) const
{
    if (rule.weight <= 0.0) {
        return false;
    }

    const bool biomeOk = rule.biomes.isEmpty() ? !namedBiomesOnly : std::ranges::any_of(rule.biomes, [this](const QString &biome) { return m_biomes.contains(biome); });
    const bool groundOk = rule.blocks.isEmpty() || std::ranges::any_of(rule.blocks, [this](const QString &block) { return m_found.contains(Node::block(block)); });
    return biomeOk && groundOk;
}

void Survey::addNaturalSpawns(bool namedBiomesOnly)
{
    for (const QcMobDefinition &mob : m_content.mobs()) {
        const QList<QcMobDefinition::SpawnRule> rules = mob.spawnRules();
        if (std::ranges::any_of(rules, [this, namedBiomesOnly](const QcMobDefinition::SpawnRule &rule) { return spawnsHere(rule, namedBiomesOnly); })) {
            ++m_found[Node::creature(mob.name)];
        }
    }
}

} // namespace

QcWorldCensus QcWorldCensus::take(const QcWorldGenerator &generator, const QcDimensionDefinition &definition, const QcContentRegistry &content, const Area &area)
{
    Survey survey(generator, content);
    const QcBlockPos middle = area.centre ? *area.centre : generator.spawnPoint();
    const QcChunkPos centre = middle.chunk();
    for (int dz = -area.radius; dz <= area.radius; dz += area.step) {
        for (int dx = -area.radius; dx <= area.radius; dx += area.step) {
            survey.visitChunk({centre.x + dx, centre.z + dz});
        }
    }
    survey.visitSitesAround(middle, area.radius * QcWorld::chunkSize);
    survey.addNaturalSpawns(definition.name != QString::fromLatin1(QcDimensionDefinition::overworldName));
    return {definition.name, survey.found()};
}

QList<QcProgressionRule> QcWorldCensus::rules() const
{
    QList<QcProgressionRule> rules;
    for (const auto [node, count] : found.asKeyValueRange()) {
        rules.append({u"found in %1"_s.arg(dimension), {Node::dimension(dimension)}, {node}, count});
    }
    return rules;
}
