#include "QcMobBehaviour.h"

#include "QcMobNavigator.h"
#include "QcMobSteering.h"
#include "QcMobUpkeep.h"
#include "QcServerWorld.h"

#include <algorithm>
#include <cmath>
#include <numbers>

QcMobBehaviour::QcMobBehaviour(std::shared_ptr<const QcContentRegistry> content, QRandomGenerator &random)
    : m_content(std::move(content))
    , m_random(random)
    , m_paths(*m_content)
    , m_villagers(m_content, m_random, m_paths)
{
    for (const QcMobDefinition &definition : m_content->mobs()) {
        m_rules.push_back(QcPathRules::of(definition));
    }
}

void QcMobBehaviour::setPathLimits(qint64 nodesPerTick, qint64 nodesPerSearch, qint64 pathLength)
{
    m_paths.setLimits(nodesPerTick, nodesPerSearch, pathLength);
}

void QcMobBehaviour::tick(QList<QcCreature> &mobs, QcServerWorld &world, const QList<QcMobManager::PlayerView> &players, Clock clock,
                          QList<QcMobManager::PlayerHit> &hits)
{
    m_paths.serve(world, mobs, [this](const QcCreature &mob) { return m_rules[std::size_t(mob.type)]; });
    m_crowd.build(mobs, m_content->mobs());
    m_villagers.startTick();
    m_tiers = {};
    ++m_tick;

    const Tick tick{world, players, clock.dt, hits, mobs, clock.worldTime};
    for (qsizetype i = 0; i < mobs.size(); ++i) {
        // Mobs rest where they are while their chunk is not loaded (QcEntities::settle): the rest of
        // the chunk and the density of the crowd decide together, the stronger one wins.
        if (!mobs[i].dormant) {
            update(i, mobs[i], tick);
        }
    }
}

void QcMobBehaviour::update(qsizetype index, QcCreature &mob, const Tick &tick)
{
    const QcMobDefinition &definition = m_content->mobs()[mob.type];
    mob.hurtTimer = std::max(0.0, mob.hurtTimer - tick.dt);
    mob.attackCooldown = std::max(0.0, mob.attackCooldown - tick.dt);
    mob.fleeTimer = std::max(0.0, mob.fleeTimer - tick.dt);
    mob.targetMemory = std::max(0.0, mob.targetMemory - tick.dt);
    // A villager in its bed lies still until it gets up (decision E-411).
    if (definition.villager && mob.villager.sleeping && m_villagers.rest(mob, villagerContext(mob, tick))) {
        ++m_tiers.full;
        return;
    }
    if (definition.flying) {
        ++m_tiers.full;
        fly(mob, definition, tick.world, tick.dt);
        return;
    }

    QcMobUpkeep::leaveBlocks(mob, definition, tick.world);
    mob.motion.inWater = QcPhysics::isLiquidAt(tick.world, QcPhysics::blockContaining(mob.motion.position + QcVec3{0.0, 0.4, 0.0}));

    // Throttled creatures think in turns, staggered by their ids.
    QcPhysics::Steering steering;
    switch (m_crowd.tierAt(index)) {
    case QcMobCrowd::Tier::Full:
        ++m_tiers.full;
        steering = think(mob, definition, tick, 1);
        break;
    case QcMobCrowd::Tier::Throttled: {
        ++m_tiers.throttled;
        const int interval = m_crowd.thinkInterval(index);
        steering = (m_tick + mob.id) % quint64(interval) == 0 ? think(mob, definition, tick, interval)
                                                              : withoutThinking(mob, definition, tick, true);
        break;
    }
    case QcMobCrowd::Tier::Idle:
        ++m_tiers.idle;
        // Villagers still think in the slowest turn and keep to their path in between: a crowd that
        // met at the bell goes home at night.
        if (definition.villager) {
            const int interval = QcMobSteering::sightCheckTicks;
            steering = (m_tick + mob.id) % quint64(interval) == 0 ? think(mob, definition, tick, interval)
                                                                  : withoutThinking(mob, definition, tick, true);
            break;
        }
        steering = withoutThinking(mob, definition, tick, false);
        break;
    }
    steerApart(index, mob, tick.world, steering);
    walk(mob, definition, tick.world, steering);
}

QcPhysics::Steering QcMobBehaviour::think(QcCreature &mob, const QcMobDefinition &definition, const Tick &tick, int thinkTicks)
{
    // Every creature looks once in sightCheckTicks, staggered by its id: one thinking every
    // `thinkTicks` (at most sightCheckTicks) looks at its thought in the first `thinkTicks` of them.
    const bool mayPlan = thinkTicks <= 1;
    // Villagers follow their routine (in a crowd in turns, update(); QcVillagerBehaviour budgets
    // their decisions).
    if (definition.villager) {
        const QcPhysics::Steering steering = m_villagers.think(mob, definition, villagerContext(mob, tick));
        mob.lastSpeed = steering.speed;
        return steering;
    }
    const bool looks = (m_tick + mob.id) % QcMobSteering::sightCheckTicks < quint64(std::max(1, thinkTicks));
    const QcMobManager::PlayerView *target =
        definition.hostile ? QcMobSteering::chooseTarget(mob, definition, tick.players, tick.world, looks) : nullptr;
    // A hostile creature that has not looked yet still strikes a player right next to it.
    if (!target && QcMobSteering::strikeAdjacent(mob, definition, tick.players, tick.world, tick.hits)) {
        mob.lastSpeed = definition.chaseSpeed;
        return {{}, definition.chaseSpeed};
    }

    const QcPhysics::Steering steering = target ? chase(mob, definition, *target, tick, mayPlan) : wander(mob, definition, tick, mayPlan);
    mob.lastSpeed = steering.speed;
    return steering;
}

QcPhysics::Steering QcMobBehaviour::withoutThinking(QcCreature &mob, const QcMobDefinition &definition, const Tick &tick, bool coasts)
{
    if (QcMobSteering::strikeAdjacent(mob, definition, tick.players, tick.world, tick.hits)) {
        return {{}, definition.chaseSpeed};
    }
    if (!coasts) {
        return {{}, definition.walkSpeed};
    }

    // Along the path it has, without searching, and never off an edge.
    QcPhysics::Steering steering = navigate(mob, mob.lastSpeed, tick.world, false).steering;
    if (!QcMobNavigator::safeStep(mob, steering.wish, tick.world, m_paths.cells(), m_rules[std::size_t(mob.type)])) {
        return {{}, steering.speed};
    }
    if (steering.wish.length() > 0.0) {
        mob.yaw = QcMobSteering::yawTowards(steering.wish);
    }
    return steering;
}

void QcMobBehaviour::steerApart(qsizetype index, const QcCreature &mob, const QcServerWorld &world, QcPhysics::Steering &steering) const
{
    // The crowd pushes apart, but not off a cliff.
    const QcVec3 push = m_crowd.push(index, mob, m_content->mobs()[mob.type].width * 0.5);
    if (push.length() <= 0.0 || !QcMobNavigator::safeStep(mob, push, world, m_paths.cells(), m_rules[std::size_t(mob.type)])) {
        return;
    }

    steering.wish += push * QcMobCrowd::strength;
    if (steering.wish.length() > 1.0) {
        steering.wish = steering.wish.normalized();
    }
    steering.speed = std::max(steering.speed, m_content->mobs()[mob.type].walkSpeed);
}

QcMobNavigator::Navigation QcMobBehaviour::navigate(QcCreature &mob, double speed, const QcServerWorld &world, bool mayPlan)
{
    const QcPathRules rules = m_rules[std::size_t(mob.type)];
    return mayPlan ? QcMobNavigator::steer(mob, speed, m_paths, world, rules) : QcMobNavigator::steerAlong(mob, speed, m_paths, world, rules);
}

void QcMobBehaviour::walk(QcCreature &mob, const QcMobDefinition &definition, const QcServerWorld &world, QcPhysics::Steering steering)
{
    // While knocked back the mob does not steer.
    if (mob.hurtTimer > QcMobManager::hurtInvulnerability * 0.5) {
        steering.wish = {};
    }
    const bool jump = mob.motion.inWater || steering.jump || (mob.blockedLastTick && steering.wish.length() > 0.0);
    const bool wasOnGround = mob.motion.onGround;
    // Feet in water (also while floating with the body above the surface).
    const bool wading = QcPhysics::isLiquidAt(world, QcPhysics::blockContaining(mob.motion.position + QcVec3{0.0, 0.1, 0.0}));
    // A swimmer at the shore, its body above the surface, leaps onto the bank as from the ground:
    // swimming up never lifts its feet onto a bank level with the water.
    if (mob.blockedLastTick && wading && !mob.motion.inWater && steering.wish.length() > 0.0) {
        mob.motion.velocity.y = std::max(mob.motion.velocity.y, QcPhysics::jumpVelocity);
    }
    const QcPhysics::MoveResult move =
        QcPhysics::stepEntity(mob.motion, QcPhysics::Steering{steering.wish, steering.speed, jump},
                              QcPhysics::BodySize{definition.width, definition.height}, world, QcPhysics::tickDuration);
    // Only a wall met while walking on the ground or swimming (the shore) is jumped at: one met in
    // the air or on landing (a head under a door's lintel) would keep it jumping.
    mob.blockedLastTick = (move.blockedX || move.blockedZ) && ((wasOnGround && mob.motion.onGround) || wading);
}

QcPhysics::Steering QcMobBehaviour::chase(QcCreature &mob, const QcMobDefinition &definition, const QcMobManager::PlayerView &target,
                                          const Tick &tick, bool mayPlan)
{
    // In melee it stands and strikes; otherwise it walks its path, within one cell of the target.
    if (QcMobSteering::inMelee(mob, definition, target, tick.world)) {
        QcMobSteering::strike(mob, definition, target, tick.hits);
        return {{}, definition.chaseSpeed};
    }

    QcMobNavigator::setGoal(mob, QcMobPath::Purpose::Chase, target.position, 1);
    const QcPhysics::Steering steering =
        navigate(mob, definition.chaseSpeed, tick.world, mayPlan).steering;
    const QcVec3 towards{target.position.x - mob.motion.position.x, 0.0, target.position.z - mob.motion.position.z};
    const QcVec3 facing = steering.wish.length() > 0.0 ? steering.wish : towards;
    if (facing.length() > 0.0) {
        mob.yaw = QcMobSteering::yawTowards(facing);
    }
    return steering;
}

QcPhysics::Steering QcMobBehaviour::wander(QcCreature &mob, const QcMobDefinition &definition, const Tick &tick, bool mayPlan)
{
    QcMobPath &path = mob.path;
    double speed = definition.walkSpeed;
    mob.wanderTimer -= tick.dt;
    if (mob.fleeTimer > 0.0) {
        // Away from the attacker; the search ends at the reachable place closest to the far point.
        if (path.purpose != QcMobPath::Purpose::Flee) {
            path.clear();
            QcMobNavigator::setGoal(mob, QcMobPath::Purpose::Flee, mob.motion.position + mob.wanderDirection * fleeDistance, 0);
        }
        speed = definition.chaseSpeed;
    } else if (path.purpose == QcMobPath::Purpose::Flee || path.purpose == QcMobPath::Purpose::Chase) {
        path.clear();
        mob.wanderTimer = 0.0;
    }

    if (mob.fleeTimer <= 0.0 && mob.wanderTimer <= 0.0) {
        // Half of the time stand still, otherwise walk to a place nearby for a few seconds.
        mob.wanderTimer = 2.0 + m_random.bounded(5.0);
        path.clear();
        if (m_random.bounded(2) == 0) {
            mob.wanderDirection = {};
        } else {
            const double angle = m_random.bounded(2.0 * std::numbers::pi);
            mob.wanderDirection = {std::sin(angle), 0.0, -std::cos(angle)};
            QcMobNavigator::setGoal(mob, QcMobPath::Purpose::Wander, mob.motion.position + mob.wanderDirection * wanderDistance, 0);
        }
    }

    const QcPhysics::Steering steering = navigate(mob, speed, tick.world, mayPlan).steering;
    // A wanderer at the end of its path stands until its time is up.
    if (path.purpose == QcMobPath::Purpose::Wander && path.finished()) {
        path.clear();
    }
    if (steering.wish.length() > 0.0) {
        mob.yaw = QcMobSteering::yawTowards(steering.wish);
    }
    return steering;
}

void QcMobBehaviour::fly(QcCreature &mob, const QcMobDefinition &definition, QcServerWorld &world, double dt)
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
    double height = QcMobManager::flyMaxHeight + 1.0;
    for (int down = 0; down <= int(QcMobManager::flyMaxHeight) + 1; ++down) {
        const QcBlockPos below = feet.offset(0, -down, 0);
        if (QcPhysics::isSolidAt(world, below) || QcPhysics::isLiquidAt(world, below)) {
            height = mob.motion.position.y - (below.y + 1.0);
            break;
        }
    }

    QcVec3 wish = mob.wanderDirection;
    if (height < QcMobManager::flyMinHeight) {
        wish.y = 0.6;
    } else if (height > QcMobManager::flyMaxHeight) {
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
        mob.yaw = QcMobSteering::yawTowards({mob.motion.velocity.x, 0.0, mob.motion.velocity.z});
    }
}
