#include "QcMobManager.h"

#include "QcDayCycle.h"
#include "QcPhysics.h"
#include "QcServerWorld.h"

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
    , m_random(quint32(seed ^ (seed >> 32)))
{
}

const QcMobManager::Mob *QcMobManager::find(quint32 id) const
{
    const auto it = std::ranges::find(m_mobs, id, &Mob::id);
    return it == m_mobs.end() ? nullptr : &*it;
}

quint32 QcMobManager::spawn(int type, const QcVec3 &position, quint32 id, bool underground)
{
    Mob mob;
    mob.underground = underground;
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

QList<QcMobManager::PlayerHit> QcMobManager::tick(QcServerWorld &world, const QList<PlayerView> &players, double worldTime,
                                                 double dt, const std::function<quint32()> &nextEntityId)
{
    QList<PlayerHit> hits;
    for (Mob &mob : m_mobs) {
        mob.hurtThisTick = false;
        think(mob, world, players, dt, hits);
    }

    // Despawn mobs far from everyone, and night creatures in daylight once nobody is close.
    const bool night = QcDayCycle::isNight(worldTime);
    m_mobs.removeIf([&](const Mob &mob) {
        double nearest = std::numeric_limits<double>::max();
        for (const PlayerView &player : players) {
            nearest = std::min(nearest, (player.position - mob.motion.position).length());
        }
        const QcMobDefinition &definition = m_content->mobs()[mob.type];
        const bool outOfTime = !night && !mob.underground && definition.spawnTime == QcMobDefinition::SpawnTime::Night
                               && nearest > 24.0;
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

void QcMobManager::trySpawnAround(QcServerWorld &world, const QcVec3 &centre, bool night, const QList<PlayerView> &players,
                                  const std::function<quint32()> &nextEntityId)
{
    int hostile = 0;
    int passive = 0;
    for (const Mob &mob : std::as_const(m_mobs)) {
        if ((mob.motion.position - centre).length() < spawnRadiusMax + 16.0) {
            (m_content->mobs()[mob.type].hostile ? hostile : passive) += 1;
        }
    }
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
        const int surface = chunk->highestNonAir(QcBlockPos{x, 0, z}.localX(), QcBlockPos{x, 0, z}.localZ());
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
        if (!content.block(ground).solid) {
            continue;
        }
        bool tooClose = false;
        for (const PlayerView &player : players) {
            tooClose = tooClose || (player.position - QcVec3{x + 0.5, double(floorY + 1), z + 0.5}).length() < spawnRadiusMin;
        }
        if (tooClose) {
            continue;
        }

        // Weighted choice among the mobs allowed here.
        QList<int> candidates;
        double totalWeight = 0.0;
        for (qsizetype type = 0; type < content.mobs().size(); ++type) {
            const QcMobDefinition &definition = content.mobs()[type];
            const bool timeOk = cave ? definition.spawnsInCaves
                                     : (definition.spawnTime == QcMobDefinition::SpawnTime::Any
                                        || (definition.spawnTime == QcMobDefinition::SpawnTime::Night) == night);
            const bool capOk = definition.hostile ? hostile < hostileCapPerPlayer : passive < passiveCapPerPlayer;
            const bool groundOk = definition.spawnBlocks.isEmpty()
                                  || definition.spawnBlocks.contains(content.block(ground).name);
            if (timeOk && capOk && groundOk && definition.spawnWeight > 0.0) {
                candidates.append(int(type));
                totalWeight += definition.spawnWeight;
            }
        }
        if (candidates.isEmpty()) {
            continue;
        }
        double pick = m_random.bounded(totalWeight);
        int chosen = candidates.constLast();
        for (const int type : std::as_const(candidates)) {
            pick -= content.mobs()[type].spawnWeight;
            if (pick < 0.0) {
                chosen = type;
                break;
            }
        }
        const QcMobDefinition &definition = content.mobs()[chosen];
        const int group = 1 + m_random.bounded(definition.maxGroup);
        const int cap = definition.hostile ? hostileCapPerPlayer : passiveCapPerPlayer;
        // The group stands on neighbouring blocks of the same height, never inside walls.
        constexpr std::array<std::pair<int, int>, 4> offsets{{{0, 0}, {1, 0}, {0, 1}, {1, 1}}};
        for (int i = 0; i < group && i < std::ssize(offsets); ++i) {
            int &count = definition.hostile ? hostile : passive;
            if (count >= cap) {
                break;
            }
            const QcBlockPos feet{x + offsets[size_t(i)].first, floorY + 1, z + offsets[size_t(i)].second};
            const auto blockAt = [&world](QcBlockPos pos) { return world.blockAt(pos).value_or(qcAirBlockId); };
            const bool standable = content.block(blockAt(feet.offset(0, -1, 0))).solid
                                   && !content.block(blockAt(feet)).solid && !content.block(blockAt(feet.offset(0, 1, 0))).solid;
            const QcBlockId floor = blockAt(feet.offset(0, -1, 0));
            const bool groundOk = definition.spawnBlocks.isEmpty() || definition.spawnBlocks.contains(content.block(floor).name);
            if (i > 0 && (!standable || !groundOk)) {
                continue;
            }
            // Each member decides for itself whether rock covers it, e.g. at the edge of a ravine.
            const std::shared_ptr<const QcChunk> column = world.chunk(feet.chunk());
            const bool covered = cave && column
                                 && column->highestNonAir(feet.localX(), feet.localZ()) > feet.y + caveDepth;
            spawn(chosen, {feet.x + 0.5, double(feet.y), feet.z + 0.5}, nextEntityId(), covered);
            count += 1;
        }
        return;
    }
}
