#include "server/mobs/QcNaturalSpawner.h"

#include "common/core/QcWorldConstants.h"
#include "server/core/QcServerWorld.h"
#include "worldgen/terrain/QcNoise.h"
#include "worldgen/villages/QcVillagePlan.h"

#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace {

constexpr double spawnInterval = 1.0;
constexpr int spawnAttemptsPerPlayer = 4;

// Mobs never spawn on or in a block that hurts on contact (magma) or holds bodies inside (cobwebs);
// the block fields decide, so mod blocks follow the same rule.
bool keepsSpawnsAway(const QcBlockDefinition &block)
{
    return block.contactDamage > 0.0 || block.slowsInside < 1.0;
}

// Whether a walker taller than two blocks (an enderman, decision E-460) has room above the two cells
// every spawn checks: the cells up to its height are not solid. Unloaded cells count as solid.
bool roomAbove(const QcServerWorld &world, QcBlockPos feet, double height)
{
    const int cells = int(std::ceil(height - 1e-6));
    for (int up = 2; up < cells; ++up) {
        const std::optional<QcBlockId> block = world.blockAt(feet.offset(0, up, 0));
        if (!block || world.content().block(*block).solid) {
            return false;
        }
    }
    return true;
}

// A group stands on these neighbouring columns.
constexpr std::array<std::pair<int, int>, 4> groupOffsets{{{0, 0}, {1, 0}, {0, 1}, {1, 1}}};

// Peaceful walkers: the persistent kind (flying creatures stay ordinary; villagers live in their
// villages, decision E-411).
// Villagers and golems belong to villages (decision E-470): they are not animals; nor are fish
// (decision E-502), which come and go with their own cap.
bool isAnimal(const QcMobDefinition &definition)
{
    return !definition.hostile && !definition.ambient && !definition.flying && !definition.villager
           && definition.ai != QcMobDefinition::Ai::Golem && !definition.aquatic;
}

bool nearAnyPlayer(const QcVec3 &position, const QList<QcNaturalSpawner::PlayerView> &players)
{
    bool tooClose = false;
    for (const QcNaturalSpawner::PlayerView &player : players) {
        tooClose = tooClose || (player.position - position).length() < QcNaturalSpawner::spawnRadiusMin;
    }
    return tooClose;
}

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
        if (isAnimal(definition) && definition.spawnWeight > 0.0) {
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
        return !block.solid && !block.liquid && !keepsSpawnsAway(block);
    };

    for (int y = std::min(chunk.highestNonAir(x, z), QcWorld::worldHeight - 3); y > 0; --y) {
        const QcBlockDefinition &block = content.block(chunk.block(x, y, z));
        // Never below water: no animals on lake or sea floors.
        if (block.liquid) {
            return -1;
        }
        if (block.solid && !keepsSpawnsAway(block) && open(y + 1) && open(y + 2)
            && (grounds.any || grounds.blocks.contains(block.name))) {
            return y;
        }
    }
    return -1;
}

} // namespace

QcNaturalSpawner::QcNaturalSpawner(QcMobManager &mobs, std::shared_ptr<const QcContentRegistry> content, quint64 seed,
                                   QRandomGenerator &random)
    : m_mobs(mobs)
    , m_content(std::move(content))
    , m_seed(seed)
    , m_random(random)
{
    for (const QcMobDefinition &definition : m_content->mobs()) {
        m_rules.append(definition.spawnRules());
    }
}

void QcNaturalSpawner::tick(QcServerWorld &world, const QList<PlayerView> &players, bool night, double dt, const NextId &nextEntityId)
{
    m_spawnTimer += dt;
    if (!m_mobs.naturalSpawning() || m_spawnTimer < spawnInterval) {
        return;
    }

    m_spawnTimer = 0.0;
    for (const PlayerView &player : players) {
        if (player.alive) {
            trySpawnAround(world, player.position, night, players, nextEntityId);
        }
    }
}

void QcNaturalSpawner::trySpawnAround(QcServerWorld &world, const QcVec3 &centre, bool night, const QList<PlayerView> &players,
                                  const NextId &nextEntityId)
{
    NearbyCounts counts = countNear(centre);
    for (int attempt = 0; attempt < spawnAttemptsPerPlayer; ++attempt) {
        std::optional<SpawnPlace> place = randomPlace(world, centre);
        if (!place || nearAnyPlayer({place->x + 0.5, double(place->floorY + 1), place->z + 0.5}, players)) {
            continue;
        }

        // Weighted choice among the mobs allowed here.
        const QList<SpawnCandidate> candidates = candidatesAt(world, *place, night, counts);
        if (candidates.isEmpty()) {
            continue;
        }
        spawnGroup(world, *place, pick(candidates), counts, nextEntityId);
        return;
    }
}

QcNaturalSpawner::NearbyCounts QcNaturalSpawner::countNear(const QcVec3 &centre) const
{
    // Animals spawn naturally only where none is awake around the player: most come with new chunks.
    NearbyCounts counts;
    for (const QcCreature &mob : m_mobs.mobs()) {
        const QcMobDefinition &kind = m_content->mobs()[mob.type];
        // Villagers and golems live in their villages and take no room from the creatures around
        // players.
        // Creatures of monster spawners count against their spawners (decision E-520), guardians
        // against their zones (decision E-591).
        if (mob.dormant || kind.villager || kind.ai == QcMobDefinition::Ai::Golem || mob.spawner || mob.guards) {
            continue;
        }
        counts.animalsNear = counts.animalsNear || (isAnimal(kind) && (mob.motion.position - centre).length() < animalSearchDistance);
        if ((mob.motion.position - centre).length() < spawnRadiusMax + 16.0) {
            counts.of(kind) += 1;
            counts.perType[mob.type] += 1;
        }
    }
    return counts;
}

std::optional<QcNaturalSpawner::SpawnPlace> QcNaturalSpawner::randomPlace(const QcServerWorld &world, const QcVec3 &centre)
{
    const double angle = m_random.bounded(2.0 * std::numbers::pi);
    const double radius = spawnRadiusMin + m_random.bounded(spawnRadiusMax - spawnRadiusMin);
    const QcBlockPos column{int(std::floor(centre.x + std::sin(angle) * radius)), 0, int(std::floor(centre.z - std::cos(angle) * radius))};
    const std::shared_ptr<const QcChunk> chunk = world.chunk(column.chunk());
    if (!chunk) {
        return std::nullopt;
    }

    const QcContentRegistry &content = *m_content;
    int surface = chunk->highestNonAir(column.localX(), column.localZ());
    // Plants (tall grass, flowers) stand on the real ground; mobs spawn in them.
    while (surface > 0) {
        const QcBlockDefinition &top = content.block(chunk->block(column.localX(), surface, column.localZ()));
        if (top.solid || top.liquid) {
            break;
        }
        --surface;
    }

    // Either the surface or, half of the time, a cave floor well below it.
    SpawnPlace place;
    place.x = column.x;
    place.z = column.z;
    place.floorY = surface;
    place.cave = m_random.bounded(2) == 0 && surface > caveDepth + 12;
    if (place.cave) {
        place.floorY = randomCaveFloor(*chunk, column, surface);
        if (place.floorY < 0) {
            return std::nullopt;
        }
    }

    place.ground = chunk->block(column.localX(), place.floorY, column.localZ());
    // Flying creatures also hover over water.
    if (!content.block(place.ground).solid && !content.block(place.ground).liquid) {
        return std::nullopt;
    }
    return place;
}

int QcNaturalSpawner::randomCaveFloor(const QcChunk &chunk, QcBlockPos column, int surface)
{
    for (int y = 8 + m_random.bounded(surface - caveDepth - 8); y > 2; --y) {
        const QcBlockId below = chunk.block(column.localX(), y - 1, column.localZ());
        const QcBlockId feet = chunk.block(column.localX(), y, column.localZ());
        const QcBlockId head = chunk.block(column.localX(), y + 1, column.localZ());
        if (m_content->block(below).solid && feet == qcAirBlockId && head == qcAirBlockId) {
            return y - 1;
        }
    }
    return -1;
}

QList<QcNaturalSpawner::SpawnCandidate> QcNaturalSpawner::candidatesAt(const QcServerWorld &world, SpawnPlace &place, bool night,
                                                               const NearbyCounts &counts) const
{
    QList<SpawnCandidate> candidates;
    for (qsizetype type = 0; type < m_content->mobs().size(); ++type) {
        const QcMobDefinition &definition = m_content->mobs()[type];
        const bool capOk = counts.of(definition) < capOf(definition)
                           && (definition.maxCount == 0 || counts.perType.value(int(type)) < definition.maxCount)
                           && !(isAnimal(definition) && counts.animalsNear);
        if (!capOk) {
            continue;
        }

        // Each kind takes the first of its rules that allows this place.
        for (const SpawnRule &rule : m_rules.at(type)) {
            if (ruleAllows(world, place, {int(type), &rule}, night)) {
                candidates.append({int(type), &rule});
                break;
            }
        }
    }

    return candidates;
}

bool QcNaturalSpawner::ruleAllows(const QcServerWorld &world, SpawnPlace &place, SpawnCandidate candidate, bool night) const
{
    const QcContentRegistry &content = *m_content;
    const QcMobDefinition &definition = content.mobs()[candidate.type];
    const SpawnRule &rule = *candidate.rule;
    const bool timeOk = place.cave ? rule.caves
                                   : (rule.time == QcMobDefinition::SpawnTime::Any || (rule.time == QcMobDefinition::SpawnTime::Night) == night);
    // Water creatures spawn in water only, never in lava (decision E-502), the others on solid ground.
    const QcBlockDefinition &ground = content.block(place.ground);
    const bool water = ground.liquid && ground.fire.setsAlightSeconds == 0;
    const bool groundOk = (rule.blocks.isEmpty() || rule.blocks.contains(ground.name))
                          && (definition.aquatic ? water : (ground.solid || definition.flying));
    const int ruleIndex = int(candidate.rule - m_rules.at(candidate.type).constData());
    if (!timeOk || !groundOk || rule.weight <= 0.0 || place.floorY + 1 > rule.maxY
        || !spawnsInChunk(candidate.type, QcBlockPos{place.x, 0, place.z}.chunk(), rule.chunkChance, ruleIndex)
        || (!definition.flying && !definition.aquatic && !roomAbove(world, {place.x, place.floorY + 1, place.z}, definition.height))) {
        return false;
    }

    if (!rule.biomes.isEmpty()) {
        if (!place.biome) {
            place.biome = world.generator().biomeAt(place.x, place.z);
        }
        if (!rule.biomes.contains(*place.biome)) {
            return false;
        }
    }
    // The light only matters for mobs that shun it.
    if (rule.maxLight < QcLightEngine::maxLevel) {
        if (!place.light) {
            place.light = world.blockLight({place.x, place.floorY + 1, place.z});
        }
        return *place.light <= rule.maxLight;
    }
    return true;
}

QcNaturalSpawner::SpawnCandidate QcNaturalSpawner::pick(const QList<SpawnCandidate> &candidates)
{
    double totalWeight = 0.0;
    for (const SpawnCandidate candidate : candidates) {
        totalWeight += candidate.rule->weight;
    }

    double pick = m_random.bounded(totalWeight);
    for (const SpawnCandidate candidate : candidates) {
        pick -= candidate.rule->weight;
        if (pick < 0.0) {
            return candidate;
        }
    }
    return candidates.constLast();
}

void QcNaturalSpawner::spawnGroup(QcServerWorld &world, const SpawnPlace &place, SpawnCandidate chosen, NearbyCounts &counts,
                              const NextId &nextEntityId)
{
    const QcMobDefinition &definition = m_content->mobs()[chosen.type];
    const int group = 1 + m_random.bounded(definition.maxGroup);
    const int cap = capOf(definition);

    // The group stands on neighbouring blocks of the same height, never inside walls.
    for (int i = 0; i < group && i < std::ssize(groupOffsets); ++i) {
        int &count = counts.of(definition);
        if (count >= cap || (definition.maxCount > 0 && counts.perType.value(chosen.type) >= definition.maxCount)) {
            break;
        }

        const bool spawned = definition.aquatic  ? spawnSwimmer(world, place, i, chosen, nextEntityId)
                             : definition.flying ? spawnFlyer(world, place, i, chosen, nextEntityId)
                                                 : spawnWalker(world, place, i, chosen, nextEntityId);
        if (spawned) {
            count += 1;
            counts.perType[chosen.type] += 1;
        }
    }
}

bool QcNaturalSpawner::spawnFlyer(const QcServerWorld &world, const SpawnPlace &place, int member, SpawnCandidate chosen,
                              const NextId &nextEntityId)
{
    const auto [dx, dz] = groupOffsets[std::size_t(member)];
    // Somewhere in the air one to three metres above the ground.
    const QcBlockPos air{place.x + dx, place.floorY + 1 + 1 + m_random.bounded(2), place.z + dz};
    const auto isAir = [&world](QcBlockPos pos) { return world.blockAt(pos).value_or(1) == qcAirBlockId; };
    if (!isAir(air) || !isAir(air.offset(0, 1, 0))) {
        return false;
    }

    m_mobs.spawn(chosen.type, {air.x + 0.5, air.y + 0.25, air.z + 0.5}, nextEntityId());
    m_mobs.editableMobs().last().nightRule = chosen.rule->time == QcMobDefinition::SpawnTime::Night;
    return true;
}

bool QcNaturalSpawner::spawnSwimmer(const QcServerWorld &world, const SpawnPlace &place, int member, SpawnCandidate chosen,
                                    const NextId &nextEntityId)
{
    const auto [dx, dz] = groupOffsets[std::size_t(member)];
    // One to three blocks below the surface, where the water goes that deep.
    const int depth = 1 + m_random.bounded(3);
    const QcBlockPos cell{place.x + dx, place.floorY - depth + 1, place.z + dz};
    for (int dy = 0; dy < depth; ++dy) {
        if (!QcPhysics::isWaterAt(world, cell.offset(0, dy, 0))) {
            return false;
        }
    }

    m_mobs.spawn(chosen.type, {cell.x + 0.5, cell.y + 0.2, cell.z + 0.5}, nextEntityId());
    return true;
}

bool QcNaturalSpawner::spawnWalker(const QcServerWorld &world, const SpawnPlace &place, int member, SpawnCandidate chosen,
                               const NextId &nextEntityId)
{
    const QcContentRegistry &content = *m_content;
    const auto [dx, dz] = groupOffsets[std::size_t(member)];
    const QcBlockPos feet{place.x + dx, place.floorY + 1, place.z + dz};
    const auto blockAt = [&world](QcBlockPos pos) { return world.blockAt(pos).value_or(qcAirBlockId); };
    const bool standable = content.block(blockAt(feet.offset(0, -1, 0))).solid && !content.block(blockAt(feet)).solid
                           && !content.block(blockAt(feet.offset(0, 1, 0))).solid
                           && roomAbove(world, feet, content.mobs()[chosen.type].height);
    const QcBlockId floor = blockAt(feet.offset(0, -1, 0));
    const bool groundOk = chosen.rule->blocks.isEmpty() || chosen.rule->blocks.contains(content.block(floor).name);
    const bool safe = !keepsSpawnsAway(content.block(floor)) && !keepsSpawnsAway(content.block(blockAt(feet)))
                      && !keepsSpawnsAway(content.block(blockAt(feet.offset(0, 1, 0))));
    if (!safe || (member > 0 && (!standable || !groundOk))) {
        return false;
    }

    // Each member decides for itself whether rock covers it, e.g. at the edge of a ravine.
    const std::shared_ptr<const QcChunk> column = world.chunk(feet.chunk());
    const bool covered = place.cave && column && column->highestNonAir(feet.localX(), feet.localZ()) > feet.y + caveDepth;
    const QcMobDefinition &definition = content.mobs()[chosen.type];
    m_mobs.spawn(chosen.type, {feet.x + 0.5, double(feet.y), feet.z + 0.5}, nextEntityId(), covered, isAnimal(definition));
    // Spawned by a night-only rule (swamp slimes): goes with the daylight like night creatures.
    m_mobs.editableMobs().last().nightRule = chosen.rule->time == QcMobDefinition::SpawnTime::Night;
    return true;
}

bool QcNaturalSpawner::spawnsInChunk(int type, QcChunkPos pos) const
{
    return spawnsInChunk(type, pos, m_content->mobs()[type].spawnChunkChance, 0);
}

bool QcNaturalSpawner::spawnsInChunk(int type, QcChunkPos pos, double chance, int rule) const
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
        kind = (kind ^ quint8(byte)) * 1099511628211ULL;
    }
    // Extra rules (index above 0) get their own set of chunks.
    return qcPositionRandom(qcMixSeed(m_seed, 5000 + kind + quint64(rule) * 7919), pos.x, 0, pos.z) < chance;
}

void QcNaturalSpawner::spawnChunkAnimals(const QcServerWorld &world, QcChunkPos pos, const QcChunk &chunk, const NextId &nextEntityId)
{
    const quint64 seed = qcMixSeed(m_seed, 4000);
    const auto random = [&](int salt) { return qcPositionRandom(seed, pos.x, salt, pos.z); };
    if (random(0) >= chunkAnimalChance) {
        return;
    }

    const QcContentRegistry &content = *m_content;
    const AnimalGrounds grounds = animalGrounds(content);
    // A random column; the group stands there and on neighbours of the same height.
    const int lx = int(random(1) * (QcWorld::chunkSize - 1));
    const int lz = int(random(2) * (QcWorld::chunkSize - 1));
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
    const int group = std::min({definition.maxGroup, 2 + int(random(4) * std::max(1, definition.maxGroup - 1)), int(groupOffsets.size())});
    for (int i = 0; i < group; ++i) {
        const int x = lx + groupOffsets[std::size_t(i)].first;
        const int z = lz + groupOffsets[std::size_t(i)].second;
        const int y = animalGroundAt(content, chunk, grounds, x, z);
        if (y != floorY
            || (!definition.spawnBlocks.isEmpty() && !definition.spawnBlocks.contains(content.block(chunk.block(x, y, z)).name))) {
            continue;
        }
        m_mobs.spawn(chosen, {pos.x * QcWorld::chunkSize + x + 0.5, double(y + 1), pos.z * QcWorld::chunkSize + z + 0.5}, nextEntityId(),
              false, true);
    }
}

void QcNaturalSpawner::spawnVillageMobs(const QcServerWorld &world, QcChunkPos pos, const NextId &nextEntityId)
{
    const std::shared_ptr<const QcVillagePlan> village = world.generator().villageFor(pos);
    if (!village) {
        return;
    }
    for (const QcVillagePlan::Mob &mob : village->mobs) {
        const std::optional<int> type = m_content->mobIndex(mob.mob);
        const QcBlockPos cell{int(std::floor(mob.at.x)), int(std::floor(mob.at.y)), int(std::floor(mob.at.z))};
        if (type && cell.chunk() == pos) {
            const quint32 id = m_mobs.spawn(*type, mob.at, nextEntityId(), false, true);
            QcCreature &creature = *m_mobs.find(id);
            creature.confined = mob.confined;
            creature.noAi = mob.noAi;
        }
    }
}

QList<int> QcNaturalSpawner::animalCandidates(const QcServerWorld &world, QcChunkPos pos, const QcChunk &chunk, QcBlockPos floor) const
{
    const QcContentRegistry &content = *m_content;
    const QcBlockDefinition &ground = content.block(chunk.block(floor.x, floor.y, floor.z));
    QList<int> candidates;
    QString biome;

    for (qsizetype type = 0; type < content.mobs().size(); ++type) {
        const QcMobDefinition &definition = content.mobs()[type];
        if (!isAnimal(definition) || definition.spawnWeight <= 0.0 || !ground.solid
            || (!definition.spawnBlocks.isEmpty() && !definition.spawnBlocks.contains(ground.name))
            || floor.y + 1 > definition.spawnMaxY || !spawnsInChunk(int(type), pos)) {
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

        candidates.append(int(type));
    }

    return candidates;
}

int QcNaturalSpawner::pickAnimal(const QList<int> &candidates, double random) const
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

void QcNaturalSpawner::setCaps(int hostile, int passive, int ambient)
{
    m_hostileCap = hostile;
    m_passiveCap = passive;
    m_ambientCap = ambient;
}

int QcNaturalSpawner::capOf(const QcMobDefinition &definition) const
{
    return definition.aquatic ? m_waterCap : (definition.ambient ? m_ambientCap : (definition.hostile ? m_hostileCap : m_passiveCap));
}
