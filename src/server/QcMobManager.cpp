#include "QcMobManager.h"

#include "QcDayCycle.h"
#include "QcLightVolume.h"
#include "QcNoise.h"
#include "QcPhysics.h"
#include "QcServerWorld.h"

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
    if (m_spawnTimer >= spawnInterval) {
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

    const PlayerView *target = nullptr;
    double targetDistance = definition.sightRange;
    if (definition.hostile) {
        for (const PlayerView &player : players) {
            const double distance = (player.position - mob.motion.position).length();
            if (player.alive && distance < targetDistance) {
                target = &player;
                targetDistance = distance;
            }
        }
    }

    QcVec3 wish;
    double speed = definition.walkSpeed;
    if (target) {
        const QcVec3 towards{target->position.x - mob.motion.position.x, 0.0, target->position.z - mob.motion.position.z};
        wish = towards.normalized();
        speed = definition.chaseSpeed;
        const bool inReach = horizontalDistance(target->position, mob.motion.position) <= definition.attackRange
                             && std::abs(target->position.y - mob.motion.position.y) <= verticalReach;
        if (inReach) {
            wish = {};
            if (mob.attackCooldown <= 0.0) {
                mob.attackCooldown = definition.attackCooldown;
                hits.append({target->entityId, definition.attackDamage, towards.normalized()});
                mob.attackedThisTick = true;
            }
        }
        if (towards.length() > 0.0) {
            mob.yaw = yawTowards(towards);
        }
    } else {
        mob.wanderTimer -= dt;
        if (mob.fleeTimer > 0.0) {
            wish = mob.wanderDirection;
            speed = definition.chaseSpeed;
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
            wish = mob.wanderDirection;
        }
        if (wish.length() > 0.0) {
            mob.yaw = yawTowards(wish);
        }
    }

    // While knocked back the mob does not steer.
    if (mob.hurtTimer > hurtInvulnerability * 0.5) {
        wish = {};
    }
    const bool jump = mob.motion.inWater || (mob.blockedLastTick && wish.length() > 0.0);
    const QcPhysics::MoveResult move = QcPhysics::stepEntity(mob.motion, wish, speed, jump, definition.width, definition.height,
                                                             world, QcPhysics::tickDuration);
    mob.blockedLastTick = move.blockedX || move.blockedZ;
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
    int hostile = 0;
    int passive = 0;
    int ambient = 0;
    QHash<int, int> perType;
    // Animals spawn naturally only where none is awake around the player: most come with new chunks.
    bool animalsNear = false;
    for (const Mob &mob : std::as_const(m_mobs)) {
        if (mob.dormant) {
            continue;
        }
        const QcMobDefinition &kind = m_content->mobs()[mob.type];
        animalsNear = animalsNear
                      || (!kind.hostile && !kind.ambient && !kind.flying && (mob.motion.position - centre).length() < animalSearchDistance);
        if ((mob.motion.position - centre).length() < spawnRadiusMax + 16.0) {
            const QcMobDefinition &definition = m_content->mobs()[mob.type];
            (definition.ambient ? ambient : (definition.hostile ? hostile : passive)) += 1;
            perType[mob.type] += 1;
        }
    }
    const auto capOf = [](const QcMobDefinition &definition) {
        return definition.ambient ? ambientCapPerPlayer : (definition.hostile ? hostileCapPerPlayer : passiveCapPerPlayer);
    };
    const QcContentRegistry &content = *m_content;
    for (int attempt = 0; attempt < spawnAttemptsPerPlayer; ++attempt) {
        const double angle = m_random.bounded(2.0 * std::numbers::pi);
        const double radius = spawnRadiusMin + m_random.bounded(spawnRadiusMax - spawnRadiusMin);
        const int x = int(std::floor(centre.x + std::sin(angle) * radius));
        const int z = int(std::floor(centre.z - std::cos(angle) * radius));
        const std::shared_ptr<const QcChunk> chunk = world.chunk(QcBlockPos{x, 0, z}.chunk());
        if (!chunk) {
            continue;
        }
        int surface = chunk->highestNonAir(QcBlockPos{x, 0, z}.localX(), QcBlockPos{x, 0, z}.localZ());
        // Plants (tall grass, flowers) stand on the real ground; mobs spawn in them.
        while (surface > 0) {
            const QcBlockDefinition &top = content.block(chunk->block(QcBlockPos{x, 0, z}.localX(), surface, QcBlockPos{x, 0, z}.localZ()));
            if (top.solid || top.liquid) {
                break;
            }
            --surface;
        }
        // Either the surface or, half of the time, a cave floor well below it.
        const bool cave = m_random.bounded(2) == 0 && surface > caveDepth + 12;
        int floorY = surface;
        if (cave) {
            floorY = -1;
            for (int y = 8 + m_random.bounded(surface - caveDepth - 8); y > 2; --y) {
                const QcBlockId below = chunk->block(QcBlockPos{x, 0, z}.localX(), y - 1, QcBlockPos{x, 0, z}.localZ());
                const QcBlockId feet = chunk->block(QcBlockPos{x, 0, z}.localX(), y, QcBlockPos{x, 0, z}.localZ());
                const QcBlockId head = chunk->block(QcBlockPos{x, 0, z}.localX(), y + 1, QcBlockPos{x, 0, z}.localZ());
                if (content.block(below).solid && feet == qcAirBlockId && head == qcAirBlockId) {
                    floorY = y - 1;
                    break;
                }
            }
            if (floorY < 0) {
                continue;
            }
        }
        const QcBlockId ground = chunk->block(QcBlockPos{x, 0, z}.localX(), floorY, QcBlockPos{x, 0, z}.localZ());
        // Flying creatures also hover over water.
        if (!content.block(ground).solid && !content.block(ground).liquid) {
            continue;
        }
        QString biome;
        bool biomeKnown = false;
        bool tooClose = false;
        for (const PlayerView &player : players) {
            tooClose = tooClose || (player.position - QcVec3{x + 0.5, double(floorY + 1), z + 0.5}).length() < spawnRadiusMin;
        }
        if (tooClose) {
            continue;
        }

        // Weighted choice among the mobs allowed here.
        // Computed once and only when a mob that shuns light could spawn here.
        std::optional<int> light;
        const auto lightHere = [&] {
            if (!light) {
                light = world.blockLightAt({x, floorY + 1, z});
            }
            return *light;
        };
        // Each kind takes the first of its rules that allows this place.
        struct Candidate
        {
            int type = 0;
            const QcMobDefinition::SpawnRule *rule = nullptr;
        };
        QList<Candidate> candidates;
        double totalWeight = 0.0;
        const QcChunkPos chunkPos = QcBlockPos{x, 0, z}.chunk();
        for (qsizetype type = 0; type < content.mobs().size(); ++type) {
            const QcMobDefinition &definition = content.mobs()[type];
            const int inCategory = definition.ambient ? ambient : (definition.hostile ? hostile : passive);
            // Animals: peaceful walkers, the persistent kind (flying creatures stay ordinary).
            const bool animal = !definition.hostile && !definition.ambient && !definition.flying;
            const bool capOk = inCategory < capOf(definition) && (definition.maxCount == 0 || perType.value(int(type)) < definition.maxCount)
                               && !(animal && animalsNear);
            if (!capOk) {
                continue;
            }
            for (const QcMobDefinition::SpawnRule &rule : m_rules.at(type)) {
                const bool timeOk = cave ? rule.caves
                                         : (rule.time == QcMobDefinition::SpawnTime::Any || (rule.time == QcMobDefinition::SpawnTime::Night) == night);
                const bool groundOk = (rule.blocks.isEmpty() || rule.blocks.contains(content.block(ground).name))
                                      && (content.block(ground).solid || definition.flying);
                if (!timeOk || !groundOk || rule.weight <= 0.0 || floorY + 1 > rule.maxY
                    || !spawnsInChunk(int(type), chunkPos, rule.chunkChance, int(&rule - m_rules.at(type).constData()))) {
                    continue;
                }
                if (!rule.biomes.isEmpty()) {
                    if (!biomeKnown) {
                        biome = world.generator().biomeAt(x, z);
                        biomeKnown = true;
                    }
                    if (!rule.biomes.contains(biome)) {
                        continue;
                    }
                }
                if (rule.maxLight < QcLightVolume::maxLevel && lightHere() > rule.maxLight) {
                    continue;
                }
                candidates.append({int(type), &rule});
                totalWeight += rule.weight;
                break;
            }
        }
        if (candidates.isEmpty()) {
            continue;
        }
        double pick = m_random.bounded(totalWeight);
        Candidate chosenCandidate = candidates.constLast();
        for (const Candidate &candidate : std::as_const(candidates)) {
            pick -= candidate.rule->weight;
            if (pick < 0.0) {
                chosenCandidate = candidate;
                break;
            }
        }
        const int chosen = chosenCandidate.type;
        const QcMobDefinition::SpawnRule &chosenRule = *chosenCandidate.rule;
        const QcMobDefinition &definition = content.mobs()[chosen];
        const int group = 1 + m_random.bounded(definition.maxGroup);
        const int cap = capOf(definition);
        // The group stands on neighbouring blocks of the same height, never inside walls.
        constexpr std::array<std::pair<int, int>, 4> offsets{{{0, 0}, {1, 0}, {0, 1}, {1, 1}}};
        for (int i = 0; i < group && i < std::ssize(offsets); ++i) {
            int &count = definition.ambient ? ambient : (definition.hostile ? hostile : passive);
            if (count >= cap || (definition.maxCount > 0 && perType.value(chosen) >= definition.maxCount)) {
                break;
            }
            if (definition.flying) {
                // Somewhere in the air one to three metres above the ground.
                const QcBlockPos air{x + offsets[size_t(i)].first, floorY + 1 + 1 + m_random.bounded(2), z + offsets[size_t(i)].second};
                const auto isAir = [&world](QcBlockPos pos) { return world.blockAt(pos).value_or(1) == qcAirBlockId; };
                if (isAir(air) && isAir(air.offset(0, 1, 0))) {
                    spawn(chosen, {air.x + 0.5, air.y + 0.25, air.z + 0.5}, nextEntityId());
                    m_mobs.last().nightRule = chosenRule.time == QcMobDefinition::SpawnTime::Night;
                    count += 1;
                    perType[chosen] += 1;
                }
                continue;
            }
            const QcBlockPos feet{x + offsets[size_t(i)].first, floorY + 1, z + offsets[size_t(i)].second};
            const auto blockAt = [&world](QcBlockPos pos) { return world.blockAt(pos).value_or(qcAirBlockId); };
            const bool standable = content.block(blockAt(feet.offset(0, -1, 0))).solid
                                   && !content.block(blockAt(feet)).solid && !content.block(blockAt(feet.offset(0, 1, 0))).solid;
            const QcBlockId floor = blockAt(feet.offset(0, -1, 0));
            const bool groundOk = chosenRule.blocks.isEmpty() || chosenRule.blocks.contains(content.block(floor).name);
            if (i > 0 && (!standable || !groundOk)) {
                continue;
            }
            // Each member decides for itself whether rock covers it, e.g. at the edge of a ravine.
            const std::shared_ptr<const QcChunk> column = world.chunk(feet.chunk());
            const bool covered = cave && column
                                 && column->highestNonAir(feet.localX(), feet.localZ()) > feet.y + caveDepth;
            spawn(chosen, {feet.x + 0.5, double(feet.y), feet.z + 0.5}, nextEntityId(), covered,
                  !definition.hostile && !definition.ambient && !definition.flying);
            // Spawned by a night-only rule (swamp slimes): goes with the daylight like night creatures.
            m_mobs.last().nightRule = chosenRule.time == QcMobDefinition::SpawnTime::Night;
            count += 1;
            perType[chosen] += 1;
        }
        return;
    }
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
    if (m_populated.contains(pos)) {
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
    // Blocks animals may stand on; empty when one of them may stand on anything solid.
    QSet<QString> grounds;
    bool anyGround = false;
    for (const QcMobDefinition &definition : content.mobs()) {
        if (!definition.hostile && !definition.ambient && !definition.flying) {
            anyGround = anyGround || definition.spawnBlocks.isEmpty();
            grounds.unite(QSet<QString>(definition.spawnBlocks.begin(), definition.spawnBlocks.end()));
        }
    }
    // The highest ground with room for an animal: below plants and under tree crowns.
    const auto groundAt = [&](int x, int z) {
        const auto open = [&](int y) {
            const QcBlockDefinition &block = content.block(chunk->block(x, y, z));
            return !block.solid && !block.liquid;
        };
        for (int y = std::min(chunk->highestNonAir(x, z), QcWorld::worldHeight - 3); y > 0; --y) {
            const QcBlockDefinition &block = content.block(chunk->block(x, y, z));
            // Never below water: no animals on lake or sea floors.
            if (block.liquid) {
                return -1;
            }
            if (block.solid && open(y + 1) && open(y + 2) && (anyGround || grounds.contains(block.name))) {
                return y;
            }
        }
        return -1;
    };
    // A random column; the group stands there and on neighbours of the same height.
    const int lx = int(random(1) * (QcWorld::chunkSize - 1));
    const int lz = int(random(2) * (QcWorld::chunkSize - 1));
    const int floorY = groundAt(lx, lz);
    if (floorY < 0) {
        return;
    }
    const QcBlockId ground = chunk->block(lx, floorY, lz);
    const int worldX = pos.x * QcWorld::chunkSize + lx;
    const int worldZ = pos.z * QcWorld::chunkSize + lz;
    QList<int> candidates;
    double totalWeight = 0.0;
    QString biome;
    for (qsizetype type = 0; type < content.mobs().size(); ++type) {
        const QcMobDefinition &definition = content.mobs()[type];
        if (definition.hostile || definition.ambient || definition.flying || definition.spawnWeight <= 0.0
            || !content.block(ground).solid
            || (!definition.spawnBlocks.isEmpty() && !definition.spawnBlocks.contains(content.block(ground).name))
            || floorY + 1 > definition.spawnMaxY || !spawnsInChunk(int(type), pos)) {
            continue;
        }
        if (!definition.spawnBiomes.isEmpty()) {
            if (biome.isEmpty()) {
                biome = world.generator().biomeAt(worldX, worldZ);
            }
            if (!definition.spawnBiomes.contains(biome)) {
                continue;
            }
        }
        candidates.append(int(type));
        totalWeight += definition.spawnWeight;
    }
    if (candidates.isEmpty()) {
        return;
    }
    double pick = random(3) * totalWeight;
    int chosen = candidates.constLast();
    for (const int type : std::as_const(candidates)) {
        pick -= content.mobs()[type].spawnWeight;
        if (pick < 0.0) {
            chosen = type;
            break;
        }
    }
    const QcMobDefinition &definition = content.mobs()[chosen];
    // Groups of two up to the kind's largest group.
    constexpr std::array<std::pair<int, int>, 4> offsets{{{0, 0}, {1, 0}, {0, 1}, {1, 1}}};
    const int group = std::min({definition.maxGroup, 2 + int(random(4) * std::max(1, definition.maxGroup - 1)), int(offsets.size())});
    for (int i = 0; i < group; ++i) {
        const int x = lx + offsets[std::size_t(i)].first;
        const int z = lz + offsets[std::size_t(i)].second;
        const int y = groundAt(x, z);
        if (y != floorY
            || (!definition.spawnBlocks.isEmpty() && !definition.spawnBlocks.contains(content.block(chunk->block(x, y, z)).name))) {
            continue;
        }
        spawn(chosen, {pos.x * QcWorld::chunkSize + x + 0.5, double(y + 1), pos.z * QcWorld::chunkSize + z + 0.5}, nextEntityId(),
              false, true);
    }
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
