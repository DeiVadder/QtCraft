#include "server/mobs/QcMobBehaviour.h"

#include "server/core/QcServerWorld.h"
#include "server/mobs/QcMobSteering.h"
#include "server/mobs/QcMobUpkeep.h"
#include "server/pathing/QcMobNavigator.h"

#include <algorithm>
#include <cmath>
#include <numbers>

QcMobBehaviour::QcMobBehaviour(std::shared_ptr<const QcContentRegistry> content, QRandomGenerator &random)
    : m_content(std::move(content))
    , m_random(random)
    , m_paths(*m_content)
    , m_villagers(m_content, m_random, m_paths)
    , m_passive(m_random, m_paths)
    , m_hostile(m_random, m_paths)
{
    for (const QcMobDefinition &definition : m_content->mobs()) {
        m_rules.push_back(QcPathRules::of(definition));
        switch (definition.ai) {
        case QcMobDefinition::Ai::Passive:
            m_brains.push_back(&m_passive);
            break;
        case QcMobDefinition::Ai::Hostile:
            m_brains.push_back(&m_hostile);
            break;
        case QcMobDefinition::Ai::Villager:
            m_brains.push_back(&m_villagers);
            break;
        }
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

    for (qsizetype i = 0; i < mobs.size(); ++i) {
        // Mobs rest where they are while their chunk is not loaded (QcEntities::settle): the rest of
        // the chunk and the density of the crowd decide together, the stronger one wins.
        if (!mobs[i].dormant) {
            const QcMobBrain::Context context{world, mobs, clock.worldTime, clock.dt, m_rules[std::size_t(mobs[i].type)], &players, &hits, m_tick};
            update(i, mobs[i], context);
        }
    }
}

void QcMobBehaviour::update(qsizetype index, QcCreature &mob, const QcMobBrain::Context &context)
{
    const QcMobDefinition &definition = m_content->mobs()[mob.type];
    QcMobBrain &brain = brainOf(mob.type);
    mob.hurtTimer = std::max(0.0, mob.hurtTimer - context.dt);
    mob.attackCooldown = std::max(0.0, mob.attackCooldown - context.dt);
    mob.fleeTimer = std::max(0.0, mob.fleeTimer - context.dt);
    mob.targetMemory = std::max(0.0, mob.targetMemory - context.dt);
    // A creature lying or sitting asks its brain whether it stays so (a villager in its bed lies
    // still until it gets up, decision E-411).
    if (mob.pose != QcPose::Standing && brain.rest(mob, context)) {
        ++m_tiers.full;
        return;
    }
    if (definition.flying) {
        ++m_tiers.full;
        fly(mob, definition, context.world, context.dt);
        return;
    }

    QcMobUpkeep::leaveBlocks(mob, definition, context.world);
    mob.motion.inWater = QcPhysics::isLiquidAt(context.world, QcPhysics::blockContaining(mob.motion.position + QcVec3{0.0, 0.4, 0.0}));

    // Throttled creatures think in turns, staggered by their ids.
    QcPhysics::Steering steering;
    switch (m_crowd.tierAt(index)) {
    case QcMobCrowd::Tier::Full:
        ++m_tiers.full;
        steering = brain.think(mob, definition, context, 1);
        break;
    case QcMobCrowd::Tier::Throttled: {
        ++m_tiers.throttled;
        const int interval = m_crowd.thinkInterval(index);
        steering = (m_tick + mob.id) % quint64(interval) == 0 ? brain.think(mob, definition, context, interval)
                                                              : withoutThinking(mob, definition, context, true);
        break;
    }
    case QcMobCrowd::Tier::Idle:
        ++m_tiers.idle;
        if (brain.thinksInIdleCrowd()) {
            const int interval = QcMobSteering::sightCheckTicks;
            steering = (m_tick + mob.id) % quint64(interval) == 0 ? brain.think(mob, definition, context, interval)
                                                                  : withoutThinking(mob, definition, context, true);
            break;
        }
        steering = withoutThinking(mob, definition, context, false);
        break;
    }
    steerApart(index, mob, context.world, steering);
    walk(mob, definition, context.world, steering);
}

QcPhysics::Steering QcMobBehaviour::withoutThinking(QcCreature &mob, const QcMobDefinition &definition, const QcMobBrain::Context &context,
                                                    bool coasts)
{
    if (QcMobSteering::strikeAdjacent(mob, definition, *context.players, context.world, *context.hits)) {
        return {{}, definition.chaseSpeed};
    }
    if (!coasts) {
        return {{}, definition.walkSpeed};
    }

    // Along the path it has, without searching, and never off an edge.
    QcPhysics::Steering steering = QcMobNavigator::steerAlong(mob, mob.lastSpeed, m_paths, context.world, context.rules).steering;
    if (!QcMobNavigator::safeStep(mob, steering.wish, context.world, m_paths.cells(), context.rules)) {
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
