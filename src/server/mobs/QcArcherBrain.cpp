#include "server/mobs/QcArcherBrain.h"

#include "common/content/registry/QcContentRegistry.h"
#include "server/core/QcServerWorld.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/mobs/QcArcherState.h"
#include "server/mobs/QcMobSteering.h"
#include "server/pathing/QcMobNavigator.h"
#include "server/pathing/QcPathPlanner.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

QcVec3 horizontal(const QcVec3 &vector)
{
    return {vector.x, 0.0, vector.z};
}

QcVec3 eyeOf(const QcCreature &mob, const QcMobDefinition &definition)
{
    return mob.motion.position + QcVec3{0.0, definition.height * QcMobSteering::eyeShare, 0.0};
}

} // namespace

QcArcherBrain::QcArcherBrain(const QcContentRegistry &content, QRandomGenerator &random, QcPathPlanner &paths)
    : QcPassiveBrain(random, paths)
    , m_content(content)
{
}

QcPhysics::Steering QcArcherBrain::think(QcCreature &mob, const QcMobDefinition &definition, const Context &context, int thinkTicks)
{
    // Every archer has its state from QcMobManager::spawn; one from elsewhere (a test) gets it here.
    if (!mob.archer.has()) {
        mob.archer.emplace();
    }
    const bool mayPlan = thinkTicks <= 1;
    const bool looks = (context.tick + mob.id) % QcMobSteering::sightCheckTicks < static_cast<quint64>(std::max(1, thinkTicks));
    const QcMobManager::PlayerView *target = QcMobSteering::chooseTarget(mob, definition, *context.players, context.world, looks);
    if (!target) {
        mob.archer->drawStart = -1;
        const QcPhysics::Steering steering = wander(mob, definition, context, mayPlan);
        mob.lastSpeed = steering.speed;
        return steering;
    }

    mob.archer->track(target->entityId, target->position, context.tick, context.dt);
    const QcVec3 aimPoint = target->position + QcVec3{0.0, aimHeight, 0.0};
    const bool sees = QcLineOfSight::seesPoint(context.world, eyeOf(mob, definition), aimPoint);
    const double distance = horizontal(target->position - mob.motion.position).length();
    // Cornered: a blow at a target in melee, besides backing off (not without blows, the witch).
    if (definition.attackDamage > 0.0 && QcMobSteering::inMelee(mob, definition, *target, context.world)) {
        if (const std::optional<QcMobManager::PlayerHit> hit = QcMobSteering::strike(mob, definition, *target)) {
            context.hits->append(*hit);
        }
    }
    drawAndShoot(mob, definition, *target, sees && distance <= definition.archer.range, context);

    const QcPhysics::Steering steering = keepDistance(mob, *target, sees, context, mayPlan);
    const QcVec3 towards = horizontal(target->position - mob.motion.position);
    if (towards.length() > 0.0) {
        mob.yaw = QcMobSteering::yawTowards(towards);
    }
    mob.lastSpeed = steering.speed;
    return steering;
}

void QcArcherState::track(quint32 targetId, const QcVec3 &position, quint64 tick, double dt)
{
    // A new target, one not seen for a while or one that jumped (a teleport) starts at rest.
    const quint64 since = tick - seenTick;
    const double elapsed = static_cast<double>(since) * dt;
    const double moved = (position - targetPosition).length();
    if (target != targetId || since == 0 || since > static_cast<quint64>(QcMobSteering::sightCheckTicks)
        || moved > QcArcherBrain::maxTargetSpeed * elapsed + QcArcherBrain::targetJump) {
        targetVelocity = {};
    } else {
        QcVec3 estimate = (position - targetPosition) * (1.0 / elapsed);
        if (estimate.length() > QcArcherBrain::maxTargetSpeed) {
            estimate = estimate.normalized() * QcArcherBrain::maxTargetSpeed;
        }
        targetVelocity = targetVelocity + (estimate - targetVelocity) * QcArcherBrain::velocitySmoothing;
    }
    target = targetId;
    seenTick = tick;
    targetPosition = position;
}

void QcArcherBrain::drawAndShoot(QcCreature &mob, const QcMobDefinition &definition, const QcMobManager::PlayerView &target,
                                 bool canShoot, const Context &context)
{
    QcArcherState &state = *mob.archer;
    const QcArcherDefinition &archer = definition.archer;
    // A blow of its own (attackCooldown runs) breaks the draw; it begins anew when the blow is over.
    // A drink (decision E-623) holds the hand.
    if (!canShoot || mob.attackedThisTick || state.drinking() || (state.drawing() && mob.attackCooldown > 0.0)) {
        state.drawStart = -1;
        return;
    }
    if (!state.drawing()) {
        if (mob.attackCooldown <= 0.0) {
            state.drawStart = static_cast<qint64>(context.tick);
        }
        return;
    }
    const qint64 drawTicks = std::max<qint64>(1, std::llround(archer.drawSeconds / QcPhysics::tickDuration));
    const QString &type = projectileFor(archer, target);
    const QcProjectileDefinition *projectile = m_content.projectile(type);
    if (static_cast<qint64>(context.tick) - state.drawStart < drawTicks || !projectile || !context.launches) {
        return;
    }

    // From just below the eyes towards the middle of the target, where it will be.
    const QcVec3 from = eyeOf(mob, definition) - QcVec3{0.0, 0.1, 0.0};
    const QcVec3 aimed = aim(from, target.position + QcVec3{0.0, aimHeight, 0.0}, state.targetVelocity, archer.speed, *projectile);
    // The aim may shoot faster or slower than `speed`; the damage stays that of `speed`.
    const double launchSpeed = aimed.length();
    const double damageFactor = launchSpeed > 0.0 ? archer.speed / launchSpeed : 1.0;
    context.launches->append(
        {mob.id, type, from, spread(aimed, archer.spreadDegrees, random()), QcProjectilePickup::Never, damageFactor});
    state.drawStart = -1;
    mob.attackCooldown = archer.cooldownSeconds;
}

const QString &QcArcherBrain::projectileFor(const QcArcherDefinition &archer, const QcMobManager::PlayerView &target)
{
    for (const QcArcherDefinition::Choice &choice : archer.choices) {
        if (!target.effects || !target.effects->find(choice.unlessTargetHas)) {
            return choice.projectile;
        }
    }
    return archer.projectile;
}

QcPhysics::Steering QcArcherBrain::keepDistance(QcCreature &mob, const QcMobManager::PlayerView &target, bool sees, const Context &context,
                                                bool mayPlan)
{
    const QcMobDefinition &definition = m_content.mobs()[mob.type];
    const QcArcherDefinition &archer = definition.archer;
    const QcVec3 towards = horizontal(target.position - mob.motion.position);
    const double distance = towards.length();
    if (!sees || distance > archer.keepDistance) {
        // Along a path until it sees the target within keepDistance.
        QcMobNavigator::setGoal(mob, QcMobPath::Purpose::Chase, target.position, 1);
        return navigate(mob, mob.archer->drawing() ? definition.walkSpeed : definition.chaseSpeed, context, mayPlan).steering;
    }

    mob.path.clear();
    const double away = distance < archer.minDistance ? 1.0 : 0.0;
    return circle(mob, definition, distance > 0.0 ? towards * (1.0 / distance) : QcVec3{0.0, 0.0, -1.0}, away, context);
}

QcPhysics::Steering QcArcherBrain::circle(QcCreature &mob, const QcMobDefinition &definition, const QcVec3 &towards, double away,
                                          const Context &context)
{
    QcArcherState &state = *mob.archer;
    if (--state.strafeTicks <= 0 || mob.blockedLastTick) {
        state.strafeTicks = strafeTicks + random().bounded(strafeTicks);
        state.strafeSide = mob.blockedLastTick || random().bounded(2) == 0 ? -state.strafeSide : state.strafeSide;
    }

    // Sideways, and straight away while backing off; the other side, or standing, at an edge.
    const QcVec3 side{-towards.z * state.strafeSide, 0.0, towards.x * state.strafeSide};
    const QcPathGround ground{context.world, paths().cells(), context.rules};
    for (const QcVec3 &wish : {(side * 0.5 - towards * (2.0 * away)).normalized(), (side * -0.5 - towards * (2.0 * away)).normalized()}) {
        if (QcMobNavigator::safeStep(mob, wish, ground)) {
            return {wish, definition.walkSpeed};
        }
    }
    return {{}, definition.walkSpeed};
}

QcVec3 QcArcherBrain::aim(const QcVec3 &from, const QcVec3 &target, const QcVec3 &targetVelocity, double speed,
                          const QcProjectileDefinition &definition)
{
    // Two rounds of the lead: the ticks to the target, then the ticks to where it will be.
    const double dt = QcPhysics::tickDuration;
    QcVec3 point = target;
    int steps = 1;
    for (int round = 0; round < 2; ++round) {
        steps = std::clamp(static_cast<int>(std::lround((point - from).length() / speed / dt)), 1, maxAimSteps);
        point = target + targetVelocity * (steps * dt);
    }

    // The flight of QcProjectileFlight::step: each tick gravity, then drag by the factor d, then the
    // move. After n ticks a launch velocity v has carried v dt (d + d² + … + dⁿ), and gravity has
    // pulled it down by g dt² times the sum over the ticks of (d + … + dⁱ).
    const double factor = std::exp(-definition.drag * dt);
    double power = 1.0;
    double carried = 0.0;
    double pulled = 0.0;
    for (int tick = 1; tick <= steps; ++tick) {
        power *= factor;
        carried += power;
        pulled += carried;
    }
    const QcVec3 delta = point - from;
    const double share = carried * dt;
    QcVec3 velocity{delta.x / share, (delta.y + definition.gravity * dt * dt * pulled) / share, delta.z / share};
    if (velocity.length() > QcProjectileDefinition::maxSpeed) {
        velocity = velocity.normalized() * QcProjectileDefinition::maxSpeed;
    }
    return velocity;
}

QcVec3 QcArcherBrain::spread(const QcVec3 &velocity, double degrees, QRandomGenerator &random)
{
    const double speed = velocity.length();
    if (degrees <= 0.0 || speed <= 0.0) {
        return velocity;
    }

    // Two directions across the flight, then a random angle around it and off it.
    const QcVec3 along = velocity * (1.0 / speed);
    const QcVec3 across = std::abs(along.y) < 0.99 ? QcVec3{-along.z, 0.0, along.x}.normalized() : QcVec3{1.0, 0.0, 0.0};
    const QcVec3 up{along.y * across.z - along.z * across.y, along.z * across.x - along.x * across.z, along.x * across.y - along.y * across.x};
    const double around = random.bounded(2.0 * std::numbers::pi);
    const double off = std::tan(random.bounded(degrees) * std::numbers::pi / 180.0);
    return (along + (across * std::cos(around) + up * std::sin(around)) * off).normalized() * speed;
}
