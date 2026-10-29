// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/spawning/QcChunkSpawns.h"

#include "common/core/QcWorldConstants.h"
#include "server/core/QcServerWorld.h"
#include "server/mobs/QcMobManager.h"
#include "worldgen/terrain/QcNoise.h"
#include "worldgen/villages/QcVillagePlan.h"

#include <QSet>

#include <algorithm>

namespace {

// Blocks animals of the chunk groups may stand on; `any` when one of them may stand on anything
// solid. Kinds that never come with a chunk (weight 0, like the showcase's mannequin) do not count:
// they would let the search stop on tree crowns, where none of the others may stand.
struct AnimalGrounds
{
    QSet<QString> blocks;
    bool any = false;
};

AnimalGrounds animalGrounds(const QcContentRegistry &content)
{
    AnimalGrounds grounds;
    for (const QcMobDefinition &definition : content.mobs()) {
        if (QcChunkSpawns::isAnimal(definition) && definition.spawnWeight > 0.0) {
            grounds.any = grounds.any || definition.spawnBlocks.isEmpty();
            grounds.blocks.unite(QSet<QString>(definition.spawnBlocks.begin(), definition.spawnBlocks.end()));
        }
    }
    return grounds;
}

// The highest ground with room for an animal: below plants and under tree crowns; -1 when none.
int animalGroundAt(const QcContentRegistry &content, const QcChunk &chunk, const AnimalGrounds &grounds, int x, int z)
{
    const auto open = [&](int y) {
        const QcBlockDefinition &block = content.block(chunk.block(x, y, z));
        return !block.solid && !block.liquid && !block.keepsSpawnsAway();
    };

    for (int y = std::min(chunk.highestNonAir(x, z), QcWorld::worldHeight - 3); y > 0; --y) {
        const QcBlockDefinition &block = content.block(chunk.block(x, y, z));
        // Never below water: no animals on lake or sea floors.
        if (block.liquid) {
            return -1;
        }
        if (block.solid && !block.keepsSpawnsAway() && open(y + 1) && open(y + 2)
            && (grounds.any || grounds.blocks.contains(block.name))) {
            return y;
        }
    }
    return -1;
}

} // namespace

bool QcChunkSpawns::isAnimal(const QcMobDefinition &definition)
{
    return !definition.hostile && !definition.ambient && !definition.flying && !definition.villager
           && definition.ai != QcMobDefinition::Ai::Golem && !definition.aquatic;
}

QcChunkSpawns::QcChunkSpawns(QcMobManager &mobs, std::shared_ptr<const QcContentRegistry> content, quint64 seed)
    : m_mobs(mobs)
    , m_content(std::move(content))
    , m_seed(seed)
{
}

void QcChunkSpawns::spawnChunkAnimals(const QcServerWorld &world, QcChunkPos pos, const QcChunk &chunk, const NextId &nextEntityId)
{
    const quint64 seed = qcMixSeed(m_seed, 4000);
    const auto random = [&](int salt) { return qcPositionRandom(seed, pos.x, salt, pos.z); };
    if (random(0) >= chunkAnimalChance) {
        return;
    }

    const QcContentRegistry &content = *m_content;
    const AnimalGrounds grounds = animalGrounds(content);
    // A random column; the group stands there and on neighbours of the same height.
    const int lx = static_cast<int>(random(1) * (QcWorld::chunkSize - 1));
    const int lz = static_cast<int>(random(2) * (QcWorld::chunkSize - 1));
    const int floorY = animalGroundAt(content, chunk, grounds, lx, lz);
    if (floorY < 0) {
        return;
    }
    const QList<int> candidates = animalCandidates(world, pos, chunk, {lx, floorY, lz});
    if (candidates.isEmpty()) {
        return;
    }

    const int chosen = pickAnimal(candidates, random(3));
    const QcMobDefinition &definition = content.mobs()[chosen];
    // Groups of two up to the kind's largest group.
    const int group = std::min({definition.maxGroup, 2 + static_cast<int>(random(4) * std::max(1, definition.maxGroup - 1)),
        static_cast<int>(groupOffsets.size())});
    for (int i = 0; i < group; ++i) {
        const int x = lx + groupOffsets[static_cast<std::size_t>(i)].first;
        const int z = lz + groupOffsets[static_cast<std::size_t>(i)].second;
        const int y = animalGroundAt(content, chunk, grounds, x, z);
        if (y != floorY
            || (!definition.spawnBlocks.isEmpty() && !definition.spawnBlocks.contains(content.block(chunk.block(x, y, z)).name))) {
            continue;
        }
        m_mobs.spawn(chosen, {pos.x * QcWorld::chunkSize + x + 0.5, static_cast<double>(y + 1), pos.z * QcWorld::chunkSize + z + 0.5}, nextEntityId(),
              false, true);
    }
}

void QcChunkSpawns::spawnVillageMobs(const QcServerWorld &world, QcChunkPos pos, const NextId &nextEntityId)
{
    const std::shared_ptr<const QcVillagePlan> village = world.generator().villageFor(pos);
    if (!village) {
        return;
    }
    for (const QcVillagePlan::Mob &mob : village->mobs) {
        const std::optional<int> type = m_content->mobIndex(mob.mob);
        const QcBlockPos cell{static_cast<int>(std::floor(mob.at.x)), static_cast<int>(std::floor(mob.at.y)), static_cast<int>(std::floor(mob.at.z))};
        if (type && cell.chunk() == pos) {
            const quint32 id = m_mobs.spawn(*type, mob.at, nextEntityId(), false, true);
            QcCreature &creature = *m_mobs.find(id);
            creature.confined = mob.confined;
            creature.noAi = mob.noAi;
        }
    }
}

QList<int> QcChunkSpawns::animalCandidates(const QcServerWorld &world, QcChunkPos pos, const QcChunk &chunk, QcBlockPos floor) const
{
    const QcContentRegistry &content = *m_content;
    const QcBlockDefinition &ground = content.block(chunk.block(floor.x, floor.y, floor.z));
    QList<int> candidates;
    QString biome;

    for (qsizetype type = 0; type < content.mobs().size(); ++type) {
        const QcMobDefinition &definition = content.mobs()[type];
        if (!QcChunkSpawns::isAnimal(definition) || definition.spawnWeight <= 0.0 || !ground.solid
            || (!definition.spawnBlocks.isEmpty() && !definition.spawnBlocks.contains(ground.name))
            || floor.y + 1 > definition.spawnMaxY || !spawnsInChunk(static_cast<int>(type), pos, definition.spawnChunkChance, 0)) {
            continue;
        }

        if (!definition.spawnBiomes.isEmpty()) {
            if (biome.isEmpty()) {
                biome = world.generator().biomeAt(pos.x * QcWorld::chunkSize + floor.x, pos.z * QcWorld::chunkSize + floor.z);
            }
            if (!definition.spawnBiomes.contains(biome)) {
                continue;
            }
        }

        candidates.append(static_cast<int>(type));
    }

    return candidates;
}

int QcChunkSpawns::pickAnimal(const QList<int> &candidates, double random) const
{
    double totalWeight = 0.0;
    for (const int type : candidates) {
        totalWeight += m_content->mobs()[type].spawnWeight;
    }

    double pick = random * totalWeight;
    for (const int type : candidates) {
        pick -= m_content->mobs()[type].spawnWeight;
        if (pick < 0.0) {
            return type;
        }
    }
    return candidates.constLast();
}

bool QcChunkSpawns::spawnsInChunk(int type, QcChunkPos pos, double chance, int rule) const
{
    const QcMobDefinition &definition = m_content->mobs()[type];
    if (chance >= 1.0) {
        return true;
    }
    // Each kind has its own set of chunks, the same for every start of the world (FNV-1a of the
    // name: stable across Qt versions, unlike qHash).
    quint64 kind = 14695981039346656037ULL;
    const QByteArray utf8 = definition.name.toUtf8();
    for (const char byte : utf8) {
        kind = (kind ^ static_cast<quint8>(byte)) * 1099511628211ULL;
    }
    // Extra rules (index above 0) get their own set of chunks.
    return qcPositionRandom(qcMixSeed(m_seed, 5000 + kind + static_cast<quint64>(rule) * 7919), pos.x, 0, pos.z) < chance;
}
