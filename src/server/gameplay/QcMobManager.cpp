#include "QcMobManager.h"

#include "QcDayCycle.h"
#include "QcLightVolume.h"
#include "QcNoise.h"
#include "QcPhysics.h"
#include "QcServerWorld.h"
#include "QcWorldConstants.h"

#include <QHash>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace {

constexpr double spawnInterval = 1.0;
constexpr int spawnAttemptsPerPlayer = 4;
// Mobs only notice and attack within this vertical distance.
constexpr double verticalReach = 2.0;
constexpr double fleeSeconds = 4.0;

double horizontalDistance(const QcVec3 &a, const QcVec3 &b)
{
    const double dx = a.x - b.x;
    const double dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

float yawTowards(const QcVec3 &direction)
{
    return float(qRadiansToDegrees(std::atan2(direction.x, -direction.z)));
}

// Mobs never spawn on or in a block that hurts on contact (magma) or holds bodies inside (cobwebs);
// the block fields decide, so mod blocks follow the same rule.
bool keepsSpawnsAway(const QcBlockDefinition &block)
{
    return block.contactDamage > 0.0 || block.slowsInside < 1.0;
}

// A group stands on these neighbouring columns.
constexpr std::array<std::pair<int, int>, 4> groupOffsets{{{0, 0}, {1, 0}, {0, 1}, {1, 1}}};

int capOf(const QcMobDefinition &definition)
{
    return definition.ambient ? QcMobManager::ambientCapPerPlayer
                              : (definition.hostile ? QcMobManager::hostileCapPerPlayer : QcMobManager::passiveCapPerPlayer);
}

// Peaceful walkers: the persistent kind (flying creatures stay ordinary).
bool isAnimal(const QcMobDefinition &definition)
{
    return !definition.hostile && !definition.ambient && !definition.flying;
}

bool nearAnyPlayer(const QcVec3 &position, const QList<QcMobManager::PlayerView> &players)
{
    bool tooClose = false;
    for (const QcMobManager::PlayerView &player : players) {
        tooClose = tooClose || (player.position - position).length() < QcMobManager::spawnRadiusMin;
    }
    return tooClose;
}

const QcMobManager::PlayerView *nearestTarget(const QcMobManager::Mob &mob, const QcMobDefinition &definition,
                                              const QList<QcMobManager::PlayerView> &players)
{
    const QcMobManager::PlayerView *target = nullptr;
    double targetDistance = definition.sightRange;
    for (const QcMobManager::PlayerView &player : players) {
        const double distance = (player.position - mob.motion.position).length();
        if (player.alive && player.targetable && distance < targetDistance) {
            target = &player;
            targetDistance = distance;
        }
    }
    return target;
}

// Steering towards the target; strikes it when it is in reach.
QcPhysics::Steering chase(QcMobManager::Mob &mob, const QcMobDefinition &definition, const QcMobManager::PlayerView &target,
                          QList<QcMobManager::PlayerHit> &hits)
{
    const QcVec3 towards{target.position.x - mob.motion.position.x, 0.0, target.position.z - mob.motion.position.z};
    QcPhysics::Steering steering{towards.normalized(), definition.chaseSpeed};
    const bool inReach = horizontalDistance(target.position, mob.motion.position) <= definition.attackRange
                         && std::abs(target.position.y - mob.motion.position.y) <= verticalReach;

    if (inReach) {
        steering.wish = {};
        if (mob.attackCooldown <= 0.0) {
            mob.attackCooldown = definition.attackCooldown;
            hits.append({target.entityId, definition.attackDamage, towards.normalized()});
            mob.attackedThisTick = true;
        }
    }

    if (towards.length() > 0.0) {
        mob.yaw = yawTowards(towards);
    }

    return steering;
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

QcMobManager::QcMobManager(std::shared_ptr<const QcContentRegistry> content, quint64 seed)
    : m_content(std::move(content))
    , m_seed(seed)
    , m_random(quint32(seed ^ (seed >> 32)))
{
    for (const QcMobDefinition &definition : m_content->mobs()) {
        m_rules.append(definition.spawnRules());
    }
}

const QcMobManager::Mob *QcMobManager::find(quint32 id) const
{
    const auto it = std::ranges::find(m_mobs, id, &Mob::id);
    return it == m_mobs.end() ? nullptr : &*it;
}

quint32 QcMobManager::spawn(int type, const QcVec3 &position, quint32 id, bool underground, bool persistent)
{
    Mob mob;
    mob.underground = underground;
    mob.persistent = persistent;
    mob.id = id;
    mob.type = type;
    mob.motion.position = position;
    mob.health = m_content->mobs()[type].maxHealth;
    mob.yaw = float(m_random.bounded(360.0));
    m_mobs.append(mob);
    return id;
}

std::optional<QList<QcItemDrop>> QcMobManager::damage(quint32 mobId, double amount, const QcVec3 &from)
{
    const auto it = std::ranges::find(m_mobs, mobId, &Mob::id);
    if (it == m_mobs.end() || it->hurtTimer > 0.0) {
        return std::nullopt;
    }

    Mob &mob = *it;
    mob.health -= amount;
    mob.hurtTimer = hurtInvulnerability;
    mob.hurtThisTick = true;

    const QcVec3 away = QcVec3{mob.motion.position.x - from.x, 0.0, mob.motion.position.z - from.z}.normalized();
    mob.motion.velocity = away * knockbackSpeed + QcVec3{0.0, 5.0, 0.0};

    const QcMobDefinition &definition = m_content->mobs()[mob.type];
    if (!definition.hostile) {
        mob.fleeTimer = fleeSeconds;
        mob.wanderDirection = away;
    }

    if (mob.health > 0.0) {
        return std::nullopt;
    }

    QList<QcItemDrop> drops = definition.drops;
    m_mobs.erase(it);
    return drops;
}

bool QcMobManager::hurt(quint32 mobId, double amount)
{
    const auto it = std::ranges::find(m_mobs, mobId, &Mob::id);
    if (it == m_mobs.end() || it->hurtTimer > 0.0) {
        return false;
    }
    it->health -= amount;
    it->hurtTimer = hurtInvulnerability;
    it->hurtThisTick = true;
    if (it->health > 0.0) {
        return false;
    }
    m_mobs.erase(it);
    return true;
}

bool QcMobManager::remove(quint32 mobId)
{
    return m_mobs.removeIf([mobId](const Mob &mob) { return mob.id == mobId; }) > 0;
}

QList<QcMobManager::PlayerHit> QcMobManager::tick(QcServerWorld &world, const QList<PlayerView> &players, double worldTime,
                                                 double dt, const std::function<quint32()> &nextEntityId)
{
    QList<PlayerHit> hits;
    for (Mob &mob : m_mobs) {
        mob.hurtThisTick = false;
        mob.attackedThisTick = false;
        // Mobs rest where they are while their chunk is not loaded.
        mob.dormant = !world.chunk(QcPhysics::blockContaining(mob.motion.position).chunk());
        if (!mob.dormant) {
            think(mob, world, players, dt, hits);
        }
    }

    // Despawn mobs far from everyone, and night creatures in daylight once nobody is close.
    const bool night = QcDayCycle::isNight(worldTime);
    m_mobs.removeIf([&](const Mob &mob) {
        // The void takes every creature, without loot (decision E-288).
        if (QcWorld::isInVoid(mob.motion.position.y)) {
            return true;
        }
        if (mob.persistent) {
            return false;
        }
        double nearest = std::numeric_limits<double>::max();
        for (const PlayerView &player : players) {
            nearest = std::min(nearest, (player.position - mob.motion.position).length());
        }
        const QcMobDefinition &definition = m_content->mobs()[mob.type];
        // Ambient night creatures go out with the daylight even next to a player.
        const bool nightCreature = definition.spawnTime == QcMobDefinition::SpawnTime::Night || mob.nightRule;
        const bool outOfTime = !night && !mob.underground && nightCreature && (nearest > 24.0 || definition.ambient);
        return nearest > despawnDistance || outOfTime;
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
    return hits;
}

void QcMobManager::think(Mob &mob, QcServerWorld &world, const QList<PlayerView> &players, double dt, QList<PlayerHit> &hits)
{
    const QcMobDefinition &definition = m_content->mobs()[mob.type];
    mob.hurtTimer = std::max(0.0, mob.hurtTimer - dt);
    mob.attackCooldown = std::max(0.0, mob.attackCooldown - dt);
    mob.fleeTimer = std::max(0.0, mob.fleeTimer - dt);
    if (definition.flying) {
        fly(mob, definition, world, dt);
        return;
    }

    mob.motion.inWater = QcPhysics::isLiquidAt(world, QcPhysics::blockContaining(mob.motion.position + QcVec3{0.0, 0.4, 0.0}));
    const PlayerView *target = definition.hostile ? nearestTarget(mob, definition, players) : nullptr;
    mob.chasing = target ? target->entityId : 0;
    QcPhysics::Steering steering = target ? chase(mob, definition, *target, hits) : wander(mob, definition, dt);

    // While knocked back the mob does not steer.
    if (mob.hurtTimer > hurtInvulnerability * 0.5) {
        steering.wish = {};
    }
    const bool jump = mob.motion.inWater || (mob.blockedLastTick && steering.wish.length() > 0.0);
    const QcPhysics::MoveResult move =
        QcPhysics::stepEntity(mob.motion, QcPhysics::Steering{steering.wish, steering.speed, jump},
                              QcPhysics::BodySize{definition.width, definition.height}, world, QcPhysics::tickDuration);
    mob.blockedLastTick = move.blockedX || move.blockedZ;
}

QcPhysics::Steering QcMobManager::wander(Mob &mob, const QcMobDefinition &definition, double dt)
{
    QcPhysics::Steering steering{{}, definition.walkSpeed};
    mob.wanderTimer -= dt;
    if (mob.fleeTimer > 0.0) {
        steering.wish = mob.wanderDirection;
        steering.speed = definition.chaseSpeed;
    } else if (mob.wanderTimer <= 0.0) {
        // Half of the time stand still, otherwise walk somewhere for a few seconds.
        mob.wanderTimer = 2.0 + m_random.bounded(5.0);
        if (m_random.bounded(2) == 0) {
            mob.wanderDirection = {};
        } else {
            const double angle = m_random.bounded(2.0 * std::numbers::pi);
            mob.wanderDirection = {std::sin(angle), 0.0, -std::cos(angle)};
        }
    }

    if (mob.fleeTimer <= 0.0) {
        steering.wish = mob.wanderDirection;
    }
    if (steering.wish.length() > 0.0) {
        mob.yaw = yawTowards(steering.wish);
    }
    return steering;
}

void QcMobManager::fly(Mob &mob, const QcMobDefinition &definition, QcServerWorld &world, double dt)
{
    mob.wanderTimer -= dt;
    if (mob.wanderTimer <= 0.0 || mob.blockedLastTick) {
        mob.wanderTimer = 1.0 + m_random.bounded(3.0);
        const double angle = m_random.bounded(2.0 * std::numbers::pi);
        mob.wanderDirection = {std::sin(angle), m_random.bounded(1.0) - 0.5, -std::cos(angle)};
    }

    // A flyer stuck in a block (summoned into a hill) rises out of it.
    const QcBlockPos centre = QcPhysics::blockContaining(mob.motion.position + QcVec3{0.0, definition.height * 0.5, 0.0});
    if (QcPhysics::isSolidAt(world, centre) && world.blockAt(centre)) {
        mob.motion.position.y = centre.y + 1.0;
        mob.motion.velocity = {};
        return;
    }

    // Height above the first solid or liquid block below (at most a few metres down).
    const QcBlockPos feet = QcPhysics::blockContaining(mob.motion.position);
    double height = flyMaxHeight + 1.0;
    for (int down = 0; down <= int(flyMaxHeight) + 1; ++down) {
        const QcBlockPos below = feet.offset(0, -down, 0);
        if (QcPhysics::isSolidAt(world, below) || QcPhysics::isLiquidAt(world, below)) {
            height = mob.motion.position.y - (below.y + 1.0);
            break;
        }
    }

    QcVec3 wish = mob.wanderDirection;
    if (height < flyMinHeight) {
        wish.y = 0.6;
    } else if (height > flyMaxHeight) {
        wish.y = -0.6;
    }

    const double blend = std::min(1.0, 4.0 * dt);
    mob.motion.velocity += (wish * definition.walkSpeed - mob.motion.velocity) * blend;

    const QcPhysics::MoveResult move =
        QcPhysics::moveBox(QcPhysics::boxAt(mob.motion.position, definition.width, definition.height), mob.motion.velocity * dt, world);
    mob.motion.position += move.applied;
    mob.motion.onGround = false;
    mob.motion.inWater = false;
    mob.blockedLastTick = move.blockedX || move.blockedY || move.blockedZ;

    if (QcVec3{mob.motion.velocity.x, 0.0, mob.motion.velocity.z}.length() > 0.05) {
        mob.yaw = yawTowards({mob.motion.velocity.x, 0.0, mob.motion.velocity.z});
    }
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
    for (const Mob &mob : m_mobs) {
        if (mob.dormant) {
            continue;
        }
        const QcMobDefinition &kind = m_content->mobs()[mob.type];
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
    if (rule.maxLight < QcLightVolume::maxLevel) {
        if (!place.light) {
            place.light = world.blockLightAt({place.x, place.floorY + 1, place.z});
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

void QcMobManager::populateChunk(const QcServerWorld &world, QcChunkPos pos, const std::function<quint32()> &nextEntityId)
{
    // Chunks seen while creatures do not spawn get their animals when they load again later.
    if (!m_naturalSpawning || m_populated.contains(pos)) {
        return;
    }
    const std::shared_ptr<const QcChunk> chunk = world.chunk(pos);
    if (!chunk) {
        return;
    }
    m_populated.insert(pos);
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

QList<QcMobManager::Stored> QcMobManager::persistentMobs() const
{
    QList<Stored> stored;
    for (const Mob &mob : m_mobs) {
        if (mob.persistent) {
            stored.append({mob.type, mob.motion.position, mob.yaw, mob.health});
        }
    }
    return stored;
}

void QcMobManager::restore(const QList<Stored> &mobs, const QSet<QcChunkPos> &populated, const std::function<quint32()> &nextEntityId)
{
    m_populated.unite(populated);
    for (const Stored &stored : mobs) {
        if (stored.type < 0 || stored.type >= m_content->mobs().size()) {
            continue;
        }

        Mob mob;
        mob.id = nextEntityId();
        mob.type = stored.type;
        mob.motion.position = stored.position;
        mob.yaw = stored.yaw;
        mob.health = std::clamp(stored.health, 0.1, m_content->mobs()[stored.type].maxHealth);
        mob.persistent = true;
        mob.dormant = true;
        m_mobs.append(mob);
    }
}

qsizetype QcMobManager::awakeCount() const
{
    return std::ranges::count_if(m_mobs, [](const Mob &mob) { return !mob.dormant; });
}
