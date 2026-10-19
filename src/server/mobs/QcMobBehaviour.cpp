#include "server/mobs/QcMobBehaviour.h"

#include "server/core/QcServerWorld.h"
#include "server/mobs/QcMobSteering.h"
#include "server/mobs/QcMobUpkeep.h"
#include "server/pathing/QcMobNavigator.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

// A flapping bird falls no faster than its kind's limit, the gravity of the coming step included;
// on every path of a tick, so also after a rest or a push.
void limitFall(QcCreature &mob, const QcMobDefinition &definition)
{
    if (definition.maxFallSpeed > 0.0) {
        const double before = std::max(0.0, definition.maxFallSpeed - QcPhysics::gravity * QcPhysics::tickDuration);
        mob.motion.velocity.y = std::max(mob.motion.velocity.y, -before);
    }
}

} // namespace

QcMobBehaviour::QcMobBehaviour(std::shared_ptr<const QcContentRegistry> content, QRandomGenerator &random)
    : m_content(std::move(content))
    , m_random(random)
    , m_paths(*m_content)
    , m_villagers(m_content, m_random, m_paths)
    , m_passive(m_random, m_paths)
    , m_hostile(m_random, m_paths)
    , m_archers(*m_content, m_random, m_paths)
    , m_golems(m_random, m_paths)
    , m_neutral(m_random, m_paths)
    , m_guardians(m_random, m_paths)
    , m_tamePassive(m_random, m_paths, m_passive)
    , m_tameNeutral(m_random, m_paths, m_neutral)
    , m_fish(m_random)
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
        case QcMobDefinition::Ai::Archer:
            m_brains.push_back(&m_archers);
            break;
        case QcMobDefinition::Ai::Golem:
            m_brains.push_back(&m_golems);
            break;
        case QcMobDefinition::Ai::Neutral:
            m_brains.push_back(&m_neutral);
            break;
        case QcMobDefinition::Ai::Guardian:
            m_brains.push_back(&m_guardians);
            break;
        }
        // A tameable kind (passive or neutral, QcMobTraitsParser) thinks tame or wild (decision E-480).
        if (definition.tameable) {
            m_brains.back() = definition.ai == QcMobDefinition::Ai::Neutral ? &m_tameNeutral : &m_tamePassive;
        }
    }
}

void QcMobBehaviour::setPathLimits(qint64 nodesPerTick, qint64 nodesPerSearch, qint64 pathLength)
{
    m_paths.setLimits(nodesPerTick, nodesPerSearch, pathLength);
}

QList<QcMobManager::PlayerHit> QcMobBehaviour::tick(QList<QcCreature> &mobs, QcServerWorld &world,
                                                    const QList<QcMobManager::PlayerView> &players, Clock clock)
{
    QList<QcMobManager::PlayerHit> hits;
    m_paths.serve(world, mobs, [this](const QcCreature &mob) { return m_rules[std::size_t(mob.type)]; });
    m_crowd.build(mobs, m_content->mobs());
    m_fish.startTick(mobs, *m_content);
    m_villagers.startTick();
    m_tiers = {};
    ++m_tick;

    for (qsizetype i = 0; i < mobs.size(); ++i) {
        // Mobs rest where they are while their chunk is not loaded (QcEntities::settle): the rest of
        // the chunk and the density of the crowd decide together, the stronger one wins.
        if (!mobs[i].dormant) {
            const QcMobBrain::Context context{.world = world,
                                              .mobs = mobs,
                                              .worldTime = clock.worldTime,
                                              .dt = clock.dt,
                                              .rules = m_rules[std::size_t(mobs[i].type)],
                                              .players = &players,
                                              .hits = &hits,
                                              .tick = m_tick,
                                              .launches = &m_launches,
                                              .creatureHits = &m_creatureHits,
                                              .blockActions = &m_blockActions,
                                              .griefing = m_griefing};
            if (m_content->mobs()[mobs[i].type].aquatic) {
                // Water creatures swim in their schools (decision E-502), outside the crowd's tiers.
                ++m_tiers.full;
                mobs[i].hurt.tick();
                mobs[i].fleeTimer = std::max(0.0, mobs[i].fleeTimer - clock.dt);
                m_fish.swim(i, mobs, m_content->mobs()[mobs[i].type], world, clock.dt);
                continue;
            }
            update(i, mobs[i], context);
        }
    }
    return hits;
}

void QcMobBehaviour::update(qsizetype index, QcCreature &mob, const QcMobBrain::Context &context)
{
    const QcMobDefinition &definition = m_content->mobs()[mob.type];
    QcMobBrain &brain = brainOf(mob.type);
    mob.hurt.tick();
    mob.attackCooldown = std::max(0.0, mob.attackCooldown - context.dt);
    mob.fleeTimer = std::max(0.0, mob.fleeTimer - context.dt);
    mob.targetMemory = std::max(0.0, mob.targetMemory - context.dt);
    mob.angerTimer = std::max(0.0, mob.angerTimer - context.dt);
    mob.teleportCooldown = std::max(0, mob.teleportCooldown - 1);
    QcMobSteering::provoke(mob, definition, *context.players);
    mob.calm = QcMobSteering::calm(mob, definition, context.worldTime);
    limitFall(mob, definition);
    if (mob.noAi) {
        ++m_tiers.idle;
        withoutMind(index, mob, definition, context.world);
        return;
    }
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

void QcMobBehaviour::withoutMind(qsizetype index, QcCreature &mob, const QcMobDefinition &definition, const QcServerWorld &world)
{
    // A flyer hovers where it is, and a creature lying or sitting stays so, as its brain would leave
    // it while it rests.
    if (definition.flying || mob.pose != QcPose::Standing) {
        return;
    }

    // Out of blocks it was pushed into, then only the crowd's push and the physics move it: it
    // falls, sinks and is knocked back, but never steers, jumps or swims (walk).
    QcMobUpkeep::leaveBlocks(mob, definition, world);
    mob.motion.inWater = QcPhysics::isLiquidAt(world, QcPhysics::blockContaining(mob.motion.position + QcVec3{0.0, 0.4, 0.0}));
    QcPhysics::Steering steering{{}, 0.0};
    steerApart(index, mob, world, steering);
    walk(mob, definition, world, steering);
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
    if (!QcMobNavigator::safeStep(mob, steering.wish, {context.world, m_paths.cells(), context.rules})) {
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
    if (push.length() <= 0.0 || !QcMobNavigator::safeStep(mob, push, {world, m_paths.cells(), m_rules[std::size_t(mob.type)]})) {
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
    if (mob.hurt.knockedBack()) {
        steering.wish = {};
    }
    // One without a mind (decision E-510) neither jumps nor swims up.
    const bool jump = !mob.noAi && (mob.motion.inWater || steering.jump || (mob.blockedLastTick && steering.wish.length() > 0.0));
    const bool wasOnGround = mob.motion.onGround;
    // Feet in water (also while floating with the body above the surface).
    const bool wading = QcPhysics::isLiquidAt(world, QcPhysics::blockContaining(mob.motion.position + QcVec3{0.0, 0.1, 0.0}));
    // A swimmer at the shore, its body above the surface, leaps onto the bank as from the ground:
    // swimming up never lifts its feet onto a bank level with the water.
    if (!mob.noAi && mob.blockedLastTick && wading && !mob.motion.inWater && steering.wish.length() > 0.0) {
        mob.motion.velocity.y = std::max(mob.motion.velocity.y, QcPhysics::jumpVelocity);
    }
    const bool climbsWalls = definition.abilities.climbs == QcMobAbilities::Climbing::Walls;
    const QcPhysics::MoveResult move =
        QcPhysics::stepEntity(mob.motion, QcPhysics::Steering{.wish = steering.wish,
                                                    .speed = steering.speed,
                                                    .jump = jump,
                                                    .hold = steering.hold,
                                                    .climbsWalls = climbsWalls},
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
