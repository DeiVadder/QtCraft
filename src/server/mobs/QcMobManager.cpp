#include "server/mobs/QcMobManager.h"

#include "common/core/QcWorldConstants.h"
#include "common/physics/QcPhysics.h"
#include "common/world/QcDayCycle.h"
#include "server/core/QcServerWorld.h"
#include "server/mobs/QcMobBehaviour.h"
#include "server/mobs/QcMobUpkeep.h"
#include "worldgen/terrain/QcNoise.h"
#include "worldgen/villages/QcVillagePlan.h"

#include <QHash>
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

// A group stands on these neighbouring columns.
constexpr std::array<std::pair<int, int>, 4> groupOffsets{{{0, 0}, {1, 0}, {0, 1}, {1, 1}}};

// Peaceful walkers: the persistent kind (flying creatures stay ordinary; villagers live in their
// villages, decision E-411).
bool isAnimal(const QcMobDefinition &definition)
{
    return !definition.hostile && !definition.ambient && !definition.flying && !definition.villager;
}

bool nearAnyPlayer(const QcVec3 &position, const QList<QcMobManager::PlayerView> &players)
{
    bool tooClose = false;
    for (const QcMobManager::PlayerView &player : players) {
        tooClose = tooClose || (player.position - position).length() < QcMobManager::spawnRadiusMin;
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

// The persistent ids of the creatures in the world.
QSet<quint64> loadedUids(const QList<QcCreature> &mobs)
{
    QSet<quint64> uids;
    for (const QcCreature &mob : mobs) {
        if (mob.persistent) {
            uids.insert(mob.uid);
        }
    }
    return uids;
}

// The id a loaded creature keeps: its saved one, unless `taken` has it. A copied chunk (by hand or by
// a tool) brings creatures whose ids are already in the world; the copy gets the next free one from
// `next`, so every id names one creature.
quint64 uniqueUid(quint64 saved, QSet<quint64> &taken, quint64 &next)
{
    quint64 uid = saved;
    if (taken.contains(uid)) {
        uid = next++;
        qWarning().noquote() << QStringLiteral("A loaded creature has the persistent id %1 of another one; it gets %2").arg(saved).arg(uid);
    }
    taken.insert(uid);
    return uid;
}

} // namespace

QcMobManager::QcMobManager(std::shared_ptr<const QcContentRegistry> content, quint64 seed)
    : m_content(std::move(content))
    , m_seed(seed)
    , m_random(quint32(seed ^ (seed >> 32)))
    , m_behaviour(std::make_unique<QcMobBehaviour>(m_content, m_random))
    , m_breeding(m_content)
    , m_golems(m_content)
{
    for (const QcMobDefinition &definition : m_content->mobs()) {
        m_rules.append(definition.spawnRules());
    }
}

QcMobManager::~QcMobManager() = default;

void QcMobManager::setPathLimits(qint64 nodesPerTick, qint64 nodesPerSearch, qint64 pathLength)
{
    m_behaviour->setPathLimits(nodesPerTick, nodesPerSearch, pathLength);
}

const QcPathPlanner &QcMobManager::paths() const
{
    return m_behaviour->paths();
}

void QcMobManager::setCrowdLimits(qint64 throttled, qint64 idle)
{
    m_behaviour->setCrowdLimits(throttled, idle);
}

QList<QcMobCrowd::Crammed> QcMobManager::crammed(int limit) const
{
    return m_behaviour->crowd().crammed(limit);
}

const QcCreature *QcMobManager::find(quint32 id) const
{
    const auto it = std::ranges::find(m_mobs, id, &QcCreature::id);
    return it == m_mobs.end() ? nullptr : &*it;
}

QcCreature *QcMobManager::find(quint32 id)
{
    const auto it = std::ranges::find(m_mobs, id, &QcCreature::id);
    return it == m_mobs.end() ? nullptr : &*it;
}

quint32 QcMobManager::spawn(int type, const QcVec3 &position, quint32 id, bool underground, bool persistent)
{
    QcCreature mob;
    mob.underground = underground;
    mob.persistent = persistent;
    mob.id = id;
    mob.type = type;
    mob.motion.position = position;
    mob.health = m_content->mobs()[type].maxHealth;
    mob.yaw = float(m_random.bounded(360.0));
    // A villager always has a profession, the unemployed one at first (decision E-411); it lives
    // on with the world. Its look comes from its id, not from the random sequence of the creatures
    // (one moving in beside a bed gets the bed's look, spawnVillagers).
    if (m_content->mobs()[type].villager) {
        mob.villager.profession = m_content->villagerSettings().unemployed;
        mob.villager.look = quint32(qcMixSeed(m_seed, 4101 + quint64(id)));
        mob.persistent = true;
    }
    // Only creatures that are saved have a persistent id (decision E-420).
    if (mob.persistent) {
        mob.uid = m_nextUid++;
    }
    m_mobs.append(mob);
    return id;
}

std::optional<QList<QcItemDrop>> QcMobManager::damage(quint32 mobId, double amount, const QcVec3 &from)
{
    const auto it = std::ranges::find(m_mobs, mobId, &QcCreature::id);
    if (it == m_mobs.end() || it->hurtTimer > 0.0) {
        return std::nullopt;
    }

    QcCreature &mob = *it;
    mob.health -= amount;
    mob.hurtTimer = hurtInvulnerability;
    mob.hurtThisTick = true;

    const QcVec3 away = QcVec3{mob.motion.position.x - from.x, 0.0, mob.motion.position.z - from.z}.normalized();
    mob.motion.velocity = away * knockbackSpeed + QcVec3{0.0, 5.0, 0.0};

    const QcMobDefinition &definition = m_content->mobs()[mob.type];
    if (!definition.hostile) {
        mob.fleeTimer = QcMobBehaviour::fleeSeconds;
        mob.wanderDirection = away;
    }

    if (mob.health > 0.0) {
        return std::nullopt;
    }

    // Young animals leave nothing behind (decision E-293).
    QList<QcItemDrop> drops = mob.isYoung() ? QList<QcItemDrop>() : definition.drops;
    depart(mob);
    m_mobs.erase(it);
    return drops;
}

bool QcMobManager::hurt(quint32 mobId, double amount)
{
    return wound(mobId, amount).has_value();
}

std::optional<QList<QcItemDrop>> QcMobManager::wound(quint32 mobId, double amount)
{
    const auto it = std::ranges::find(m_mobs, mobId, &QcCreature::id);
    if (it == m_mobs.end()) {
        return std::nullopt;
    }
    return woundAt(it - m_mobs.begin(), amount);
}

std::optional<QList<QcItemDrop>> QcMobManager::woundAt(qsizetype index, double amount)
{
    const auto it = m_mobs.begin() + index;
    if (it->hurtTimer > 0.0) {
        return std::nullopt;
    }
    it->health -= amount;
    it->hurtTimer = hurtInvulnerability;
    it->hurtThisTick = true;
    if (it->health > 0.0) {
        return std::nullopt;
    }

    // Young animals leave nothing behind (decision E-293).
    QList<QcItemDrop> drops = it->isYoung() ? QList<QcItemDrop>() : m_content->mobs()[it->type].drops;
    depart(*it);
    m_mobs.erase(it);
    return drops;
}

bool QcMobManager::remove(quint32 mobId)
{
    const auto it = std::ranges::find(m_mobs, mobId, &QcCreature::id);
    if (it == m_mobs.end()) {
        return false;
    }
    depart(*it);
    m_mobs.erase(it);
    return true;
}

QList<QcMobManager::PlayerHit> QcMobManager::tick(QcServerWorld &world, const QList<PlayerView> &players, double worldTime,
                                                 double dt, const std::function<quint32()> &nextEntityId)
{
    QList<PlayerHit> hits;
    for (QcCreature &mob : m_mobs) {
        mob.hurtThisTick = false;
        mob.attackedThisTick = false;
    }
    m_behaviour->tick(m_mobs, world, players, {worldTime, dt}, hits);

    // Despawn mobs far from everyone, and night creatures in daylight once nobody is close.
    const bool night = QcDayCycle::isNight(worldTime);
    m_mobs.removeIf([&](const QcCreature &mob) {
        const bool despawns = QcMobUpkeep::despawns(mob, m_content->mobs()[mob.type], players, night);
        if (despawns) {
            depart(mob);
        }
        return despawns;
    });

    m_spawnTimer += dt;
    if (m_naturalSpawning && m_spawnTimer >= spawnInterval) {
        m_spawnTimer = 0.0;
        for (const PlayerView &player : players) {
            if (player.alive) {
                trySpawnAround(world, player.position, night, players, nextEntityId);
            }
        }
    }
    if (--m_villageLifeIn <= 0) {
        m_villageLifeIn = villageLifeTicks;
        liveInVillages(world, worldTime, nextEntityId);
    }
    return hits;
}

void QcMobManager::trySpawnAround(QcServerWorld &world, const QcVec3 &centre, bool night, const QList<PlayerView> &players,
                                  const std::function<quint32()> &nextEntityId)
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

QcMobManager::NearbyCounts QcMobManager::countNear(const QcVec3 &centre) const
{
    // Animals spawn naturally only where none is awake around the player: most come with new chunks.
    NearbyCounts counts;
    for (const QcCreature &mob : m_mobs) {
        const QcMobDefinition &kind = m_content->mobs()[mob.type];
        // Villagers live in their villages and take no room from the creatures around players.
        if (mob.dormant || kind.villager) {
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

std::optional<QcMobManager::SpawnPlace> QcMobManager::randomPlace(const QcServerWorld &world, const QcVec3 &centre)
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

int QcMobManager::randomCaveFloor(const QcChunk &chunk, QcBlockPos column, int surface)
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

QList<QcMobManager::SpawnCandidate> QcMobManager::candidatesAt(const QcServerWorld &world, SpawnPlace &place, bool night,
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

bool QcMobManager::ruleAllows(const QcServerWorld &world, SpawnPlace &place, SpawnCandidate candidate, bool night) const
{
    const QcContentRegistry &content = *m_content;
    const QcMobDefinition &definition = content.mobs()[candidate.type];
    const SpawnRule &rule = *candidate.rule;
    const bool timeOk = place.cave ? rule.caves
                                   : (rule.time == QcMobDefinition::SpawnTime::Any || (rule.time == QcMobDefinition::SpawnTime::Night) == night);
    const bool groundOk = (rule.blocks.isEmpty() || rule.blocks.contains(content.block(place.ground).name))
                          && (content.block(place.ground).solid || definition.flying);
    const int ruleIndex = int(candidate.rule - m_rules.at(candidate.type).constData());
    if (!timeOk || !groundOk || rule.weight <= 0.0 || place.floorY + 1 > rule.maxY
        || !spawnsInChunk(candidate.type, QcBlockPos{place.x, 0, place.z}.chunk(), rule.chunkChance, ruleIndex)) {
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

QcMobManager::SpawnCandidate QcMobManager::pick(const QList<SpawnCandidate> &candidates)
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

void QcMobManager::spawnGroup(QcServerWorld &world, const SpawnPlace &place, SpawnCandidate chosen, NearbyCounts &counts,
                              const std::function<quint32()> &nextEntityId)
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

        const bool spawned = definition.flying ? spawnFlyer(world, place, i, chosen, nextEntityId)
                                               : spawnWalker(world, place, i, chosen, nextEntityId);
        if (spawned) {
            count += 1;
            counts.perType[chosen.type] += 1;
        }
    }
}

bool QcMobManager::spawnFlyer(const QcServerWorld &world, const SpawnPlace &place, int member, SpawnCandidate chosen,
                              const std::function<quint32()> &nextEntityId)
{
    const auto [dx, dz] = groupOffsets[std::size_t(member)];
    // Somewhere in the air one to three metres above the ground.
    const QcBlockPos air{place.x + dx, place.floorY + 1 + 1 + m_random.bounded(2), place.z + dz};
    const auto isAir = [&world](QcBlockPos pos) { return world.blockAt(pos).value_or(1) == qcAirBlockId; };
    if (!isAir(air) || !isAir(air.offset(0, 1, 0))) {
        return false;
    }

    spawn(chosen.type, {air.x + 0.5, air.y + 0.25, air.z + 0.5}, nextEntityId());
    m_mobs.last().nightRule = chosen.rule->time == QcMobDefinition::SpawnTime::Night;
    return true;
}

bool QcMobManager::spawnWalker(const QcServerWorld &world, const SpawnPlace &place, int member, SpawnCandidate chosen,
                               const std::function<quint32()> &nextEntityId)
{
    const QcContentRegistry &content = *m_content;
    const auto [dx, dz] = groupOffsets[std::size_t(member)];
    const QcBlockPos feet{place.x + dx, place.floorY + 1, place.z + dz};
    const auto blockAt = [&world](QcBlockPos pos) { return world.blockAt(pos).value_or(qcAirBlockId); };
    const bool standable = content.block(blockAt(feet.offset(0, -1, 0))).solid && !content.block(blockAt(feet)).solid
                           && !content.block(blockAt(feet.offset(0, 1, 0))).solid;
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
    spawn(chosen.type, {feet.x + 0.5, double(feet.y), feet.z + 0.5}, nextEntityId(), covered, isAnimal(definition));
    // Spawned by a night-only rule (swamp slimes): goes with the daylight like night creatures.
    m_mobs.last().nightRule = chosen.rule->time == QcMobDefinition::SpawnTime::Night;
    return true;
}

bool QcMobManager::spawnsInChunk(int type, QcChunkPos pos) const
{
    return spawnsInChunk(type, pos, m_content->mobs()[type].spawnChunkChance, 0);
}

bool QcMobManager::spawnsInChunk(int type, QcChunkPos pos, double chance, int rule) const
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

void QcMobManager::populateChunk(QcServerWorld &world, QcChunkPos pos, const std::function<quint32()> &nextEntityId)
{
    // Chunks seen while creatures do not spawn get their animals when they load again later; a
    // village that is always lived in (the showcase's rows) gets residents for its free beds anyway,
    // each time a chunk of it arrives.
    checkVillageClaims(world, pos);
    if (!m_naturalSpawning) {
        if (world.chunk(pos)) {
            spawnVillagers(world, pos, nextEntityId, true);
            spawnWaitingVillagers(world, pos, nextEntityId, true);
        }
        return;
    }
    if (m_populated.contains(pos)) {
        return;
    }
    const std::shared_ptr<const QcChunk> chunk = world.chunk(pos);
    if (!chunk) {
        return;
    }
    m_populated.insert(pos);
    spawnVillageMobs(world, pos, nextEntityId);
    spawnVillagers(world, pos, nextEntityId, false);
    spawnWaitingVillagers(world, pos, nextEntityId, false);
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
    const int floorY = animalGroundAt(content, *chunk, grounds, lx, lz);
    if (floorY < 0) {
        return;
    }
    const QList<int> candidates = animalCandidates(world, pos, *chunk, {lx, floorY, lz});
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
        const int y = animalGroundAt(content, *chunk, grounds, x, z);
        if (y != floorY
            || (!definition.spawnBlocks.isEmpty() && !definition.spawnBlocks.contains(content.block(chunk->block(x, y, z)).name))) {
            continue;
        }
        spawn(chosen, {pos.x * QcWorld::chunkSize + x + 0.5, double(y + 1), pos.z * QcWorld::chunkSize + z + 0.5}, nextEntityId(),
              false, true);
    }
}

void QcMobManager::spawnVillageMobs(const QcServerWorld &world, QcChunkPos pos, const std::function<quint32()> &nextEntityId)
{
    const std::shared_ptr<const QcVillagePlan> village = world.generator().villageFor(pos);
    if (!village) {
        return;
    }
    for (const QcVillagePlan::Mob &mob : village->mobs) {
        const std::optional<int> type = m_content->mobIndex(mob.mob);
        const QcBlockPos cell{int(std::floor(mob.at.x)), int(std::floor(mob.at.y)), int(std::floor(mob.at.z))};
        if (type && cell.chunk() == pos) {
            spawn(*type, mob.at, nextEntityId(), false, true);
        }
    }
}

QList<int> QcMobManager::animalCandidates(const QcServerWorld &world, QcChunkPos pos, const QcChunk &chunk, QcBlockPos floor) const
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

int QcMobManager::pickAnimal(const QList<int> &candidates, double random) const
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

QcMobManager::Stored QcMobManager::storedOf(const QcCreature &mob)
{
    // Only villagers have a profession (QcMobManager::spawn).
    const std::optional<QcVillagerRecord> villager =
        mob.villager.profession.isEmpty() ? std::nullopt : std::optional<QcVillagerRecord>(mob.villager);
    return {mob.type, mob.motion.position, mob.yaw, mob.health, mob.breedCooldown, mob.growUp, mob.bred, villager, mob.uid, mob.kept};
}

QList<QcMobManager::Stored> QcMobManager::persistentMobs() const
{
    QList<Stored> stored;
    for (const QcCreature &mob : m_mobs) {
        if (mob.persistent) {
            stored.append(storedOf(mob));
        }
    }
    return stored;
}

void QcMobManager::restore(const QList<Stored> &mobs, const QSet<QcChunkPos> &populated, const std::function<quint32()> &nextEntityId)
{
    m_populated.unite(populated);
    QSet<quint64> uids = loadedUids(m_mobs);
    for (const Stored &stored : mobs) {
        if (stored.type < 0 || stored.type >= m_content->mobs().size()) {
            continue;
        }

        QcCreature mob;
        mob.id = nextEntityId();
        mob.type = stored.type;
        mob.motion.position = stored.position;
        mob.yaw = stored.yaw;
        mob.health = std::clamp(stored.health, 0.1, m_content->mobs()[stored.type].maxHealth);
        mob.persistent = true;
        mob.dormant = true;
        mob.breedCooldown = std::max(0.0, stored.breedCooldown);
        mob.growUp = std::max(0.0, stored.growUp);
        mob.bred = stored.bred;
        mob.kept = stored.kept;
        // The counter is saved with the chunks in one journal, so it lies beyond every saved id; this
        // only guards against a world.json put back by hand.
        m_nextUid = std::max(m_nextUid, stored.uid + 1);
        mob.uid = uniqueUid(stored.uid, uids, m_nextUid);
        if (m_content->mobs()[stored.type].villager) {
            static_cast<QcVillagerRecord &>(mob.villager) = stored.villager.value_or(QcVillagerRecord{});
            // A profession the content no longer has (a mod was removed): unemployed.
            if (!m_content->villagerSettings().profession(mob.villager.profession)) {
                mob.villager.profession = m_content->villagerSettings().unemployed;
            }
        }
        m_mobs.append(mob);
    }
}

qsizetype QcMobManager::awakeCount() const
{
    return std::ranges::count_if(m_mobs, [](const QcCreature &mob) { return !mob.dormant; });
}

qsizetype QcMobManager::awakeSummonedCount() const
{
    return std::ranges::count_if(m_mobs, [](const QcCreature &mob) { return mob.summoned && !mob.dormant; });
}

void QcMobManager::setCaps(int hostile, int passive, int ambient)
{
    m_hostileCap = hostile;
    m_passiveCap = passive;
    m_ambientCap = ambient;
}

int QcMobManager::capOf(const QcMobDefinition &definition) const
{
    return definition.ambient ? m_ambientCap : (definition.hostile ? m_hostileCap : m_passiveCap);
}
