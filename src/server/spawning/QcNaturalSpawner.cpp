#include "server/spawning/QcNaturalSpawner.h"

#include "common/core/QcWorldConstants.h"
#include "common/physics/QcSpawnGround.h"
#include "server/core/QcServerWorld.h"
#include "worldgen/terrain/QcNoise.h"
#include "worldgen/villages/QcVillagePlan.h"

#include <QScopeGuard>
#include <QVarLengthArray>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace {

constexpr double spawnInterval = 1.0;
constexpr int spawnAttemptsPerPlayer = 4;

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

// Distance from `position` to the nearest player; the largest double without players.
double nearestPlayer(const QcVec3 &position, const QList<QcNaturalSpawner::PlayerView> &players)
{
    double nearest = std::numeric_limits<double>::max();
    for (const QcNaturalSpawner::PlayerView &player : players) {
        nearest = std::min(nearest, (player.position - position).length());
    }
    return nearest;
}

// Whether a monster may appear with its feet at `feet`: at least monsterRadiusMin from every player
// with a body and at most monsterRadiusMax from the nearest; spectators and the dead do not count, as
// in the original game.
bool inMonsterRange(const QcVec3 &feet, const QList<QcNaturalSpawner::PlayerView> &players)
{
    double nearest = std::numeric_limits<double>::max();
    for (const QcNaturalSpawner::PlayerView &player : players) {
        if (player.alive) {
            nearest = std::min(nearest, (player.position - feet).length());
        }
    }
    return nearest >= QcNaturalSpawner::monsterRadiusMin && nearest <= QcNaturalSpawner::monsterRadiusMax;
}

// Creatures that do not count against the caps around players: villagers and golems live in their
// villages, creatures of monster spawners count against their spawners (decision E-520), guardians
// against their zones (decision E-591).
bool countsNearPlayers(const QcCreature &mob, const QcMobDefinition &kind)
{
    return !mob.dormant && !kind.villager && kind.ai != QcMobDefinition::Ai::Golem && !mob.spawner && !mob.guards;
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

QcNaturalSpawner::QcNaturalSpawner(QcMobManager &mobs, std::shared_ptr<const QcContentRegistry> content, quint64 seed,
                                   QRandomGenerator &random)
    : m_mobs(mobs)
    , m_content(std::move(content))
    , m_seed(seed)
    , m_random(random)
    , m_monsterRandom(quint32(qcMixSeed(seed, 6000)))
    , m_draws(&random)
{
    for (const QcMobDefinition &definition : m_content->mobs()) {
        m_rules.append(definition.spawnRules());
    }
}

void QcNaturalSpawner::tick(QcServerWorld &world, const QList<PlayerView> &players, Sky sky, double dt, const NextId &nextEntityId)
{
    m_spawnTimer += dt;
    if (!m_mobs.naturalSpawning()) {
        return;
    }
    spawnMonsters(world, players, sky, nextEntityId);

    if (m_spawnTimer < spawnInterval) {
        return;
    }
    m_spawnTimer = 0.0;
    for (const PlayerView &player : players) {
        if (player.alive) {
            trySpawnAround(world, player.position, sky, players, nextEntityId);
        }
    }
}

void QcNaturalSpawner::trySpawnAround(QcServerWorld &world, const QcVec3 &centre, Sky sky, const QList<PlayerView> &players,
                                  const NextId &nextEntityId)
{
    const NearbyCounts counts = countNear(centre, spawnRadiusMax + 16.0);
    for (int attempt = 0; attempt < spawnAttemptsPerPlayer; ++attempt) {
        std::optional<SpawnPlace> place = randomPlace(world, centre);
        if (!place || nearestPlayer({place->x + 0.5, double(place->floorY + 1), place->z + 0.5}, players) < spawnRadiusMin) {
            continue;
        }

        // Weighted choice among the mobs allowed here.
        const QList<SpawnCandidate> candidates = candidatesAt(world, *place, sky, counts, Pass::Others);
        if (candidates.isEmpty()) {
            continue;
        }
        const SpawnCandidate chosen = pick(candidates);
        const QcMobDefinition &definition = m_content->mobs()[chosen.type];
        spawnGroup(world, *place, chosen, std::min(capOf(definition) - counts.of(definition), counts.roomOfKind(definition, chosen.type)),
                   nextEntityId);
        return;
    }
}

// The chunks of the living players: the centres of their monster areas.
QList<QcChunkPos> QcNaturalSpawner::areaCentres(const QList<PlayerView> &players)
{
    QList<QcChunkPos> centres;
    for (const PlayerView &player : players) {
        if (player.alive) {
            centres.append(QcPhysics::blockContaining(player.position).chunk());
        }
    }
    return centres;
}

QcMonsterCap QcNaturalSpawner::monsterCap(const QcServerWorld &world, const QList<PlayerView> &players) const
{
    const QList<QcChunkPos> centres = areaCentres(players);
    const auto loaded = [&world](QcChunkPos chunk) { return world.chunk(chunk) != nullptr; };
    return withMonsters(QcMonsterCap(m_hostileCap, centres, QcMonsterCap::loadedChunksOf(centres, loaded)));
}

QcMonsterCap QcNaturalSpawner::withMonsters(QcMonsterCap cap) const
{
    for (const QcCreature &mob : m_mobs.mobs()) {
        const QcMobDefinition &kind = m_content->mobs()[mob.type];
        // Kept monsters (persistent: named ones, those placed with the world) take no room, as in the
        // original game.
        if (isMonster(kind) && countsNearPlayers(mob, kind) && !mob.persistent) {
            cap.add(QcPhysics::blockContaining(mob.motion.position).chunk());
        }
    }
    return cap;
}

QcMonsterCap QcNaturalSpawner::currentMonsterCap(const QcServerWorld &world, const QList<PlayerView> &players)
{
    // The loaded chunks of the areas are counted again at once when a player changed chunks, and
    // when chunks were loaded or unloaded since the last count, at most once a second: a walking
    // player loads chunks in most ticks. The monsters are counted every tick.
    const QList<QcChunkPos> centres = areaCentres(players);
    m_areaTicks += 1;
    if (centres != m_areaCentres || (world.loadedEpoch() != m_areaEpoch && m_areaTicks >= areaRecountTicks)) {
        const auto loaded = [&world](QcChunkPos chunk) { return world.chunk(chunk) != nullptr; };
        m_areaCentres = centres;
        m_areaChunks = QcMonsterCap::loadedChunksOf(centres, loaded);
        m_areaEpoch = world.loadedEpoch();
        m_areaTicks = 0;
    }
    return withMonsters(QcMonsterCap(m_hostileCap, centres, m_areaChunks));
}

void QcNaturalSpawner::spawnMonsters(QcServerWorld &world, const QList<PlayerView> &players, Sky sky, const NextId &nextEntityId)
{
    if (std::ranges::none_of(players, &PlayerView::alive)) {
        return;
    }
    // Monsters draw from their own sequence, their yaws included: trying places every tick leaves the
    // creatures' sequence (QcMobManager) as it was.
    m_draws = &m_monsterRandom;
    m_mobs.setYawRandom(&m_monsterRandom);
    const auto back = qScopeGuard([this] {
        m_draws = &m_random;
        m_mobs.setYawRandom(nullptr);
    });
    QcMonsterCap cap = currentMonsterCap(world, players);
    for (const PlayerView &player : players) {
        for (int attempt = 0; attempt < monsterAttemptsPerTick && player.alive && cap.hasRoom(); ++attempt) {
            const std::optional<SpawnPlace> place = randomMonsterPlace(world, player.position);
            const QcChunkPos chunk = place ? QcBlockPos{place->x, 0, place->z}.chunk() : QcChunkPos{};
            if (!place || !inMonsterRange({place->x + 0.5, double(place->floorY + 1), place->z + 0.5}, players) || cap.roomIn(chunk) <= 0) {
                continue;
            }
            const int spawned = spawnMonsterAt(world, *place, sky, cap.roomIn(chunk), nextEntityId);
            for (int i = 0; i < spawned; ++i) {
                cap.add(chunk);
            }
        }
    }
}

int QcNaturalSpawner::spawnMonsterAt(QcServerWorld &world, SpawnPlace place, Sky sky, int room, const NextId &nextEntityId)
{
    // Most places fail the light test: the kinds are checked first, without counts.
    QList<SpawnCandidate> candidates = candidatesAt(world, place, sky, NearbyCounts{}, Pass::Monsters);
    if (candidates.isEmpty()) {
        return 0;
    }

    // Kinds with a limit of their own (maxCount) count within the area around the place; the
    // creatures are counted only when such a kind may come here.
    const auto limited = [this](SpawnCandidate candidate) { return m_content->mobs()[candidate.type].maxCount > 0; };
    NearbyCounts counts;
    if (std::ranges::any_of(candidates, limited)) {
        counts = countNear({place.x + 0.5, double(place.floorY + 1), place.z + 0.5}, (QcMonsterCap::reach + 1) * double(QcWorld::chunkSize));
        candidates.removeIf([&](SpawnCandidate candidate) { return counts.roomOfKind(m_content->mobs()[candidate.type], candidate.type) <= 0; });
    }
    if (candidates.isEmpty()) {
        return 0;
    }
    const SpawnCandidate chosen = pick(candidates);
    const int kindRoom = counts.roomOfKind(m_content->mobs()[chosen.type], chosen.type);
    return spawnGroup(world, place, chosen, std::min(room, kindRoom), nextEntityId);
}

QcNaturalSpawner::NearbyCounts QcNaturalSpawner::countNear(const QcVec3 &centre, double radius) const
{
    // Animals spawn naturally only where none is awake around the player: most come with new chunks.
    NearbyCounts counts;
    for (const QcCreature &mob : m_mobs.mobs()) {
        const QcMobDefinition &kind = m_content->mobs()[mob.type];
        if (!countsNearPlayers(mob, kind)) {
            continue;
        }
        counts.animalsNear = counts.animalsNear || (isAnimal(kind) && (mob.motion.position - centre).length() < animalSearchDistance);
        if ((mob.motion.position - centre).length() < radius) {
            counts.add(kind, mob.type);
        }
    }
    return counts;
}

std::optional<QcNaturalSpawner::SpawnPlace> QcNaturalSpawner::randomPlace(const QcServerWorld &world, const QcVec3 &centre)
{
    const double angle = m_draws->bounded(2.0 * std::numbers::pi);
    const double radius = spawnRadiusMin + m_draws->bounded(spawnRadiusMax - spawnRadiusMin);
    return placeInColumn(world, {int(std::floor(centre.x + std::sin(angle) * radius)), 0, int(std::floor(centre.z - std::cos(angle) * radius))});
}

std::optional<QcNaturalSpawner::SpawnPlace> QcNaturalSpawner::randomMonsterPlace(const QcServerWorld &world, const QcVec3 &centre)
{
    // A random column of a random chunk of the area, so every loaded chunk gets its share as in the
    // original game.
    const QcChunkPos middle = QcPhysics::blockContaining(centre).chunk();
    const int span = 2 * QcMonsterCap::reach + 1;
    const int chunkX = middle.x - QcMonsterCap::reach + m_draws->bounded(span);
    const int chunkZ = middle.z - QcMonsterCap::reach + m_draws->bounded(span);
    const int x = chunkX * QcWorld::chunkSize + m_draws->bounded(QcWorld::chunkSize);
    const int z = chunkZ * QcWorld::chunkSize + m_draws->bounded(QcWorld::chunkSize);
    const std::shared_ptr<const QcChunk> chunk = world.chunk(QcBlockPos{x, 0, z}.chunk());
    if (!chunk) {
        return std::nullopt;
    }

    // One of the floors of the column, each as likely: the open surface, the ground under trees and
    // roofs, cave floors.
    const QcBlockPos column{x, 0, z};
    const int top = std::min(chunk->highestNonAir(column.localX(), column.localZ()) + 1, QcWorld::worldHeight - 2);
    const auto passable = [&](int y) {
        const QcBlockDefinition &block = m_content->block(chunk->block(column.localX(), y, column.localZ()));
        return !block.solid && !block.liquid;
    };
    QVarLengthArray<int, 32> floors;
    for (int y = top; y > 1; --y) {
        if (m_content->block(chunk->block(column.localX(), y - 1, column.localZ())).solid && passable(y) && passable(y + 1)) {
            floors.append(y - 1);
        }
    }
    if (floors.isEmpty()) {
        return std::nullopt;
    }

    SpawnPlace place;
    place.x = x;
    place.z = z;
    place.floorY = floors[qsizetype(m_draws->bounded(int(floors.size())))];
    place.cave = top - 1 > place.floorY + 1 + caveDepth;
    place.ground = chunk->block(column.localX(), place.floorY, column.localZ());
    return place;
}

std::optional<QcNaturalSpawner::SpawnPlace> QcNaturalSpawner::placeInColumn(const QcServerWorld &world, QcBlockPos column)
{
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
    place.cave = m_draws->bounded(2) == 0 && surface > caveDepth + 12;
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
    for (int y = 8 + m_draws->bounded(surface - caveDepth - 8); y > 2; --y) {
        const QcBlockId below = chunk.block(column.localX(), y - 1, column.localZ());
        const QcBlockId feet = chunk.block(column.localX(), y, column.localZ());
        const QcBlockId head = chunk.block(column.localX(), y + 1, column.localZ());
        if (m_content->block(below).solid && feet == qcAirBlockId && head == qcAirBlockId) {
            return y - 1;
        }
    }
    return -1;
}

QList<QcNaturalSpawner::SpawnCandidate> QcNaturalSpawner::candidatesAt(const QcServerWorld &world, SpawnPlace &place, Sky sky,
                                                                       const NearbyCounts &counts, Pass pass) const
{
    QList<SpawnCandidate> candidates;
    for (qsizetype type = 0; type < m_content->mobs().size(); ++type) {
        const QcMobDefinition &definition = m_content->mobs()[type];
        // Monsters have their own cap (QcMonsterCap), which the caller checked.
        const bool monster = isMonster(definition);
        const bool capOk = monster == (pass == Pass::Monsters) && (monster || counts.of(definition) < capOf(definition))
                           && (definition.maxCount == 0 || counts.perType.value(int(type)) < definition.maxCount)
                           && !(isAnimal(definition) && counts.animalsNear);
        if (!capOk) {
            continue;
        }

        // Each kind takes the first of its rules that allows this place.
        for (const SpawnRule &rule : m_rules.at(type)) {
            if (ruleAllows(world, place, {int(type), &rule}, sky)) {
                candidates.append({int(type), &rule});
                break;
            }
        }
    }

    return candidates;
}

bool QcNaturalSpawner::ruleAllows(const QcServerWorld &world, SpawnPlace &place, SpawnCandidate candidate, Sky sky) const
{
    const QcContentRegistry &content = *m_content;
    const QcMobDefinition &definition = content.mobs()[candidate.type];
    const SpawnRule &rule = *candidate.rule;
    const bool timeOk = place.cave ? rule.caves
                                   : (rule.time == QcMobDefinition::SpawnTime::Any || (rule.time == QcMobDefinition::SpawnTime::Night) == sky.night);
    // Water creatures spawn in water only, never in lava (decision E-502), flying ones over any solid
    // block or liquid, walkers on ground that carries them (QcSpawnGround).
    const QcBlockDefinition &ground = content.block(place.ground);
    const bool water = ground.liquid && ground.fire.setsAlightSeconds == 0;
    const bool groundOk = (rule.blocks.isEmpty() || rule.blocks.contains(ground.name))
                          && (definition.aquatic ? water : (definition.flying || QcSpawnGround::carries(content, place.ground)));
    if (!timeOk || !groundOk || rule.weight <= 0.0 || place.floorY + 1 > rule.maxY) {
        return false;
    }
    // The monsters' light test, or the block light only, for mobs that shun it: two light lookups,
    // before the chunk hash, the cells above and the biome.
    const bool lightOk = rule.darkness ? place.darkEnough(world, sky, rule.maxLight, *m_draws)
                                       : rule.maxLight >= QcLightEngine::maxLevel || place.lightIn(world) <= rule.maxLight;
    const int ruleIndex = int(candidate.rule - m_rules.at(candidate.type).constData());
    if (!lightOk || !spawnsInChunk(candidate.type, QcBlockPos{place.x, 0, place.z}.chunk(), rule.chunkChance, ruleIndex)
        || (!definition.flying && !definition.aquatic && !roomAbove(world, {place.x, place.floorY + 1, place.z}, definition.height))) {
        return false;
    }
    return rule.biomes.isEmpty() || rule.biomes.contains(place.biomeIn(world));
}

const QString &QcNaturalSpawner::SpawnPlace::biomeIn(const QcServerWorld &world)
{
    if (!biome) {
        biome = world.generator().biomeAt(x, z);
    }
    return *biome;
}

int QcNaturalSpawner::SpawnPlace::lightIn(const QcServerWorld &world)
{
    if (!light) {
        light = world.blockLight({x, floorY + 1, z});
    }
    return *light;
}

int QcNaturalSpawner::SpawnPlace::skyLightIn(const QcServerWorld &world)
{
    if (!skyLight) {
        skyLight = world.skyLight({x, floorY + 1, z});
    }
    return *skyLight;
}

bool QcNaturalSpawner::SpawnPlace::darkEnough(const QcServerWorld &world, Sky sky, int maxBlockLight, QRandomGenerator &random)
{
    if (!draws) {
        draws = QcSpawnLight::draw(random);
    }
    return QcSpawnLight::darkEnough({skyLightIn(world), lightIn(world), sky.darkening, maxBlockLight}, draws.value_or(QcSpawnLight::Draws{}));
}

QcNaturalSpawner::SpawnCandidate QcNaturalSpawner::pick(const QList<SpawnCandidate> &candidates)
{
    double totalWeight = 0.0;
    for (const SpawnCandidate candidate : candidates) {
        totalWeight += candidate.rule->weight;
    }

    double pick = m_draws->bounded(totalWeight);
    for (const SpawnCandidate candidate : candidates) {
        pick -= candidate.rule->weight;
        if (pick < 0.0) {
            return candidate;
        }
    }
    return candidates.constLast();
}

int QcNaturalSpawner::spawnGroup(QcServerWorld &world, const SpawnPlace &place, SpawnCandidate chosen, int room, const NextId &nextEntityId)
{
    const QcMobDefinition &definition = m_content->mobs()[chosen.type];
    const int group = 1 + m_draws->bounded(definition.maxGroup);

    // The group stands on neighbouring blocks of the same height, never inside walls.
    int spawned = 0;
    for (int i = 0; i < group && i < std::ssize(groupOffsets) && spawned < room; ++i) {
        const bool member = definition.aquatic  ? spawnSwimmer(world, place, i, chosen, nextEntityId)
                            : definition.flying ? spawnFlyer(world, place, i, chosen, nextEntityId)
                                                : spawnWalker(world, place, i, chosen, nextEntityId);
        spawned += member;
    }
    return spawned;
}

bool QcNaturalSpawner::spawnFlyer(const QcServerWorld &world, const SpawnPlace &place, int member, SpawnCandidate chosen,
                              const NextId &nextEntityId)
{
    const auto [dx, dz] = groupOffsets[std::size_t(member)];
    // Somewhere in the air one to three metres above the ground.
    const QcBlockPos air{place.x + dx, place.floorY + 1 + 1 + m_draws->bounded(2), place.z + dz};
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
    const int depth = 1 + m_draws->bounded(3);
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
    // Unloaded cells count as solid.
    const auto blockAt = [&world](QcBlockPos pos) { return world.blockAt(pos).value_or(QcBlockId(1)); };
    const QcBlockId floor = blockAt(feet.offset(0, -1, 0));
    const bool standable = QcSpawnGround::carries(content, floor) && QcSpawnGround::leavesRoom(content, blockAt(feet))
                           && QcSpawnGround::leavesRoom(content, blockAt(feet.offset(0, 1, 0)))
                           && roomAbove(world, feet, content.mobs()[chosen.type].height);
    const bool groundOk = chosen.rule->blocks.isEmpty() || chosen.rule->blocks.contains(content.block(floor).name);
    if (!standable || !groundOk) {
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
