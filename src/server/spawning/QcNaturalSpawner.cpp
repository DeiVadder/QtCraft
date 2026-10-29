// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/spawning/QcNaturalSpawner.h"

#include "common/core/QcWorldConstants.h"
#include "common/physics/QcSpawnGround.h"
#include "server/core/QcServerWorld.h"
#include "worldgen/terrain/QcNoise.h"

#include <QScopeGuard>
#include <QVarLengthArray>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <tuple>
#include <utility>

namespace {

constexpr double spawnInterval = 1.0;
constexpr int spawnAttemptsPerPlayer = 4;

// Whether a walker taller than two blocks (a shadewalker, decision E-460) has room above the two cells
// every spawn checks: the cells up to its height are not solid. Unloaded cells count as solid.
bool roomAbove(const QcServerWorld &world, QcBlockPos feet, double height)
{
    const int cells = static_cast<int>(std::ceil(height - 1e-6));
    for (int up = 2; up < cells; ++up) {
        const std::optional<QcBlockId> block = world.blockAt(feet.offset(0, up, 0));
        if (!block || world.content().block(*block).solid) {
            return false;
        }
    }
    return true;
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

} // namespace

QcNaturalSpawner::QcNaturalSpawner(QcMobManager &mobs, std::shared_ptr<const QcContentRegistry> content, quint64 seed,
                                   QRandomGenerator &random)
    : m_mobs(mobs)
    , m_content(std::move(content))
    , m_random(random)
    , m_monsterRandom(static_cast<quint32>(qcMixSeed(seed, 6000)))
    , m_chunks(mobs, m_content, seed)
    , m_weights(*m_content)
    , m_areas(m_content)
    , m_draws(&random)
{
    for (const QcMobDefinition &definition : m_content->mobs()) {
        m_rules.append(definition.spawnRules());
        m_areaRules.append(QcSpawnAreas::ruleOf(definition));
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
        if (!place || nearestPlayer({place->x + 0.5, static_cast<double>(place->floorY + 1), place->z + 0.5}, players) < spawnRadiusMin) {
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
    m_areas.refresh(world, players);
    QcMonsterCap cap = currentMonsterCap(world, players);
    // monsterAttemptsPerTick attempts for each player in turn, while there is room.
    for (qsizetype attempt = 0; attempt < players.size() * monsterAttemptsPerTick && cap.hasRoom(); ++attempt) {
        const PlayerView &player = players.at(attempt / monsterAttemptsPerTick);
        const QList<SpawnPlace> places = player.alive ? randomMonsterPlaces(world, player.position) : QList<SpawnPlace>{};
        for (const SpawnPlace &place : places) {
            const QcChunkPos chunk = QcBlockPos{place.x, 0, place.z}.chunk();
            if (!inMonsterRange({place.x + 0.5, static_cast<double>(place.floorY + 1), place.z + 0.5}, players) || cap.roomIn(chunk) <= 0) {
                continue;
            }
            const int spawned = spawnMonsterAt(world, place, sky, cap.roomIn(chunk), nextEntityId);
            for (int i = 0; i < spawned; ++i) {
                cap.add(chunk);
            }
        }
    }
}

int QcNaturalSpawner::spawnMonsterAt(QcServerWorld &world, SpawnPlace place, Sky sky, int room, const NextId &nextEntityId)
{
    // In a spawn area of a structure only its kinds come, over the whole height (decision E-871).
    if (const QcSpawnAreas::Area *area = m_areas.at(place.x, place.z)) {
        return spawnInArea(world, place, sky, *area, nextEntityId);
    }

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
        counts = countNear({place.x + 0.5, static_cast<double>(place.floorY + 1), place.z + 0.5},
            (QcMonsterCap::reach + 1) * static_cast<double>(QcWorld::chunkSize));
        candidates.removeIf([&](SpawnCandidate candidate) { return counts.roomOfKind(m_content->mobs()[candidate.type], candidate.type) <= 0; });
    }
    if (candidates.isEmpty()) {
        return 0;
    }
    // One kind of the biome's whole list by weight (decision E-838): a kind that may not spawn here
    // wastes the attempt, as in the reference.
    const std::optional<SpawnCandidate> chosen = pickOfBiome(candidates, place.biomeIn(world));
    if (!chosen) {
        return 0;
    }
    const int kindRoom = counts.roomOfKind(m_content->mobs()[chosen->type], chosen->type);
    return spawnGroup(world, place, *chosen, std::min(room, kindRoom), nextEntityId);
}

int QcNaturalSpawner::spawnInArea(QcServerWorld &world, SpawnPlace place, Sky sky, const QcSpawnAreas::Area &area, const NextId &nextEntityId)
{
    if (area.types.isEmpty()) {
        return 0;
    }
    // As in the original game: the kinds of the area alike, one at a time, and the light test, the
    // ground and the room for the body as anywhere else (the caller checked distance and cap).
    const int type = area.types.at(static_cast<qsizetype>(m_draws->bounded(static_cast<int>(area.types.size()))));
    const SpawnCandidate chosen{type, &m_areaRules.at(type)};
    if (!ruleAllows(world, place, chosen, sky)) {
        return 0;
    }
    return spawnGroup(world, place, chosen, 1, nextEntityId);
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
        counts.animalsNear = counts.animalsNear || (QcChunkSpawns::isAnimal(kind) && (mob.motion.position - centre).length() < animalSearchDistance);
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
    return placeInColumn(world,
        {static_cast<int>(std::floor(centre.x + std::sin(angle) * radius)), 0,
        static_cast<int>(std::floor(centre.z - std::cos(angle) * radius))});
}

QList<QcNaturalSpawner::SpawnPlace> QcNaturalSpawner::randomMonsterPlaces(const QcServerWorld &world, const QcVec3 &centre)
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
        return {};
    }

    // One of the floors of the column, each as likely: the open surface, the ground under trees and
    // roofs, cave floors, and the beds of water (drowned, decision E-838).
    const QcBlockPos column{x, 0, z};
    const int top = std::min(chunk->highestNonAir(column.localX(), column.localZ()) + 1, QcWorld::worldHeight - 2);
    const auto blockAt = [&](int y) -> const QcBlockDefinition & { return m_content->block(chunk->block(column.localX(), y, column.localZ())); };
    const auto passable = [&](int y) { return !blockAt(y).solid && !blockAt(y).liquid; };
    const auto water = [&](int y) { return blockAt(y).isWater(); };
    QVarLengthArray<std::pair<int, bool>, 32> floors;
    for (int y = top; y > 1; --y) {
        if (!blockAt(y - 1).solid) {
            continue;
        }
        if (passable(y) && passable(y + 1)) {
            floors.append({y - 1, false});
        } else if (water(y) && !blockAt(y + 1).solid) {
            floors.append({y - 1, true});
        }
    }
    if (floors.isEmpty()) {
        return {};
    }

    const auto placeOn = [&](std::pair<int, bool> floor) {
        SpawnPlace place;
        place.x = x;
        place.z = z;
        std::tie(place.floorY, place.water) = floor;
        place.cave = top - 1 > place.floorY + 1 + caveDepth;
        place.ground = chunk->block(column.localX(), place.floorY, column.localZ());
        return place;
    };
    // In a spawn area every floor is a place of its own (decision E-871): each storey of a farm gets
    // the rate of a single floor, as with the original game's random height.
    // The floors come from a random one on, so the first ones do not take the room of the cap first.
    if (m_areas.at(x, z)) {
        const qsizetype first = m_draws->bounded(static_cast<int>(floors.size()));
        QList<SpawnPlace> places;
        for (qsizetype i = 0; i < floors.size(); ++i) {
            places.append(placeOn(floors[(first + i) % floors.size()]));
        }
        return places;
    }
    return {placeOn(floors[static_cast<qsizetype>(m_draws->bounded(static_cast<int>(floors.size())))])};
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
                           && (definition.maxCount == 0 || counts.perType.value(static_cast<int>(type)) < definition.maxCount)
                           && !(QcChunkSpawns::isAnimal(definition) && counts.animalsNear);
        if (!capOk) {
            continue;
        }

        // Each kind takes the first of its rules that allows this place.
        for (const SpawnRule &rule : m_rules.at(type)) {
            if (ruleAllows(world, place, {static_cast<int>(type), &rule}, sky)) {
                candidates.append({static_cast<int>(type), &rule});
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
    const bool water = ground.isWater();
    const bool groundOk = (rule.blocks.isEmpty() || rule.blocks.contains(ground.name))
                          && (definition.aquatic ? water : (definition.flying || QcSpawnGround::carries(content, place.ground)));
    // Walkers spawn either in water (drowned) or on dry ground (decision E-838).
    if (!timeOk || !groundOk || rule.water != place.water || rule.weight <= 0.0 || place.floorY + 1 > rule.maxY) {
        return false;
    }
    // The monsters' light test, or the block light only, for mobs that shun it: two light lookups,
    // before the chunk hash, the cells above and the biome.
    const bool lightOk = rule.darkness ? place.darkEnough(world, sky, rule.maxLight, *m_draws)
                                       : rule.maxLight >= QcLightEngine::maxLevel || place.lightIn(world) <= rule.maxLight;
    // The index of the kind's rule matters only for a share of chunks (rules of spawn areas have none).
    const int ruleIndex = rule.chunkChance < 1.0 ? static_cast<int>(candidate.rule - m_rules.at(candidate.type).constData()) : 0;
    // Husks and strays only under the open sky, like the reference's test of the sky light.
    if (rule.sky && place.skyLightIn(world) < QcLightEngine::maxLevel) {
        return false;
    }
    if (!lightOk || !m_chunks.spawnsInChunk(candidate.type, QcBlockPos{place.x, 0, place.z}.chunk(), rule.chunkChance, ruleIndex)
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

std::optional<QcNaturalSpawner::SpawnCandidate> QcNaturalSpawner::pickOfBiome(const QList<SpawnCandidate> &candidates, const QString &biome)
{
    double weights = 0.0;
    for (const SpawnCandidate candidate : candidates) {
        weights += m_weights.weight(candidate.type, biome);
    }

    double pick = m_draws->bounded(std::max(weights, m_weights.total(biome)));
    for (const SpawnCandidate candidate : candidates) {
        pick -= m_weights.weight(candidate.type, biome);
        if (pick < 0.0) {
            return candidate;
        }
    }
    return std::nullopt;
}

int QcNaturalSpawner::spawnGroup(QcServerWorld &world, const SpawnPlace &place, SpawnCandidate chosen, int room, const NextId &nextEntityId)
{
    const QcMobDefinition &definition = m_content->mobs()[chosen.type];
    const int group = 1 + m_draws->bounded(definition.maxGroup);

    // The group stands on neighbouring blocks of the same height, never inside walls. Monsters stay
    // on their side of the edge of a spawn area (decision E-871): its kinds inside, the others out.
    const QcSpawnAreas::Area *area = isMonster(definition) ? m_areas.at(place.x, place.z) : nullptr;
    int spawned = 0;
    for (int i = 0; i < group && i < std::ssize(QcChunkSpawns::groupOffsets) && spawned < room; ++i) {
        const auto [dx, dz] = QcChunkSpawns::groupOffsets[static_cast<std::size_t>(i)];
        if (isMonster(definition) && m_areas.at(place.x + dx, place.z + dz) != area) {
            continue;
        }
        const bool member = definition.aquatic    ? spawnSwimmer(world, place, i, chosen, nextEntityId)
                            : definition.flying   ? spawnFlyer(world, place, i, chosen, nextEntityId)
                            : chosen.rule->water ? spawnWader(world, place, i, chosen, nextEntityId)
                                                  : spawnWalker(world, place, i, chosen, nextEntityId);
        spawned += member;
    }
    return spawned;
}

bool QcNaturalSpawner::spawnFlyer(const QcServerWorld &world, const SpawnPlace &place, int member, SpawnCandidate chosen,
                              const NextId &nextEntityId)
{
    const auto [dx, dz] = QcChunkSpawns::groupOffsets[static_cast<std::size_t>(member)];
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
    const auto [dx, dz] = QcChunkSpawns::groupOffsets[static_cast<std::size_t>(member)];
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

bool QcNaturalSpawner::spawnWader(const QcServerWorld &world, const SpawnPlace &place, int member, SpawnCandidate chosen,
                                  const NextId &nextEntityId)
{
    // Feet in water above a solid block, the head in water or air, as in the reference.
    const auto [dx, dz] = QcChunkSpawns::groupOffsets[static_cast<std::size_t>(member)];
    const QcBlockPos feet{place.x + dx, place.floorY + 1, place.z + dz};
    const QcContentRegistry &content = *m_content;
    const std::optional<QcBlockId> floor = world.blockAt(feet.offset(0, -1, 0));
    const std::optional<QcBlockId> head = world.blockAt(feet.offset(0, 1, 0));
    if (!floor || !head || !content.block(*floor).solid || content.block(*head).solid || !QcPhysics::isWaterAt(world, feet)) {
        return false;
    }

    m_mobs.spawn(chosen.type, {feet.x + 0.5, static_cast<double>(feet.y), feet.z + 0.5}, nextEntityId(), place.cave);
    return true;
}

bool QcNaturalSpawner::spawnWalker(const QcServerWorld &world, const SpawnPlace &place, int member, SpawnCandidate chosen,
                               const NextId &nextEntityId)
{
    const QcContentRegistry &content = *m_content;
    const auto [dx, dz] = QcChunkSpawns::groupOffsets[static_cast<std::size_t>(member)];
    const QcBlockPos feet{place.x + dx, place.floorY + 1, place.z + dz};
    // Unloaded cells count as solid.
    const auto blockAt = [&world](QcBlockPos pos) { return world.blockAt(pos).value_or(static_cast<QcBlockId>(1)); };
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
    m_mobs.spawn(chosen.type, {feet.x + 0.5, static_cast<double>(feet.y), feet.z + 0.5}, nextEntityId(), covered, QcChunkSpawns::isAnimal(definition));
    // Spawned by a night-only rule (swamp slimes): goes with the daylight like night creatures.
    m_mobs.editableMobs().last().nightRule = chosen.rule->time == QcMobDefinition::SpawnTime::Night;
    return true;
}

bool QcNaturalSpawner::spawnsInChunk(int type, QcChunkPos pos) const
{
    return m_chunks.spawnsInChunk(type, pos, m_content->mobs()[type].spawnChunkChance, 0);
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
