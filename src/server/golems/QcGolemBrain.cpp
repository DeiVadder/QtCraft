#include "server/golems/QcGolemBrain.h"

#include "server/core/QcServerWorld.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/mobs/QcMobSteering.h"
#include "server/villages/QcVillage.h"

#include <algorithm>
#include <cmath>

namespace {

double horizontalDistance(const QcVec3 &a, const QcVec3 &b)
{
    return std::hypot(a.x - b.x, a.z - b.z);
}

QcVec3 eyeOf(const QcCreature &mob, const QcMobDefinition &definition)
{
    return mob.motion.position + QcVec3{0.0, definition.height * QcMobSteering::eyeShare, 0.0};
}

// Whether `other` is prey for a golem: an awake hostile creature within its sight range.
bool isPrey(const QcCreature &mob, const QcMobDefinition &definition, const QcCreature &other, const QcContentRegistry &content)
{
    return other.id != mob.id && !other.dormant && other.health > 0.0 && content.mobs()[other.type].hostile
           && horizontalDistance(other.motion.position, mob.motion.position) < definition.sightRange
           && std::abs(other.motion.position.y - mob.motion.position.y) <= QcGolemBrain::verticalSight;
}

} // namespace

QcPhysics::Steering QcGolemBrain::think(QcCreature &mob, const QcMobDefinition &definition, const Context &context, int thinkTicks)
{
    const bool mayPlan = thinkTicks <= 1;
    const bool looks = (context.tick + mob.id) % QcMobSteering::sightCheckTicks < static_cast<quint64>(std::max(1, thinkTicks));
    mob.fleeTimer = 0.0;

    // A player it is angry with comes first, then the hostile creature it hunts.
    if (const QcMobManager::PlayerView *player = enemy(mob, definition, context)) {
        const QcPhysics::BodySize body{QcPhysics::playerWidth, QcPhysics::playerHeight};
        if (inReach(mob, definition, player->position, body, context.world)) {
            if (readyToStrike(mob, definition, player->position)) {
                const QcVec3 towards{player->position.x - mob.motion.position.x, 0.0, player->position.z - mob.motion.position.z};
                context.hits->append(
                    {.playerEntityId = player->entityId, .damage = definition.attackDamage, .knockback = towards.normalized(), .lift = definition.golem.lift, .mob = mob.id});
            }
            return {{}, definition.chaseSpeed};
        }
        return pursue(mob, definition, player->position, context, mayPlan);
    }

    if (const QcCreature *hunted = prey(mob, definition, context, looks)) {
        const QcMobDefinition &kind = context.world.content().mobs()[hunted->type];
        if (inReach(mob, definition, hunted->motion.position, {kind.width, kind.height}, context.world)) {
            if (readyToStrike(mob, definition, hunted->motion.position) && context.creatureHits) {
                context.creatureHits->append(
                    {.attacker = mob.id, .target = hunted->id, .damage = definition.attackDamage, .from = mob.motion.position, .lift = definition.golem.lift});
            }
            return {{}, definition.chaseSpeed};
        }
        return pursue(mob, definition, hunted->motion.position, context, mayPlan);
    }

    keepToVillage(mob, context, definition.walkSpeed);
    const QcPhysics::Steering steering = wander(mob, definition, context, mayPlan);
    mob.lastSpeed = steering.speed;
    return steering;
}

void QcGolemBrain::keepToVillage(QcCreature &mob, const Context &context, double walkSpeed)
{
    // Golems a player built go where they like; so do golems far from every village.
    if (!mob.golem || mob.golem->playerBuilt || mob.fleeTimer > 0.0) {
        return;
    }
    const QcBlockPos at = QcPhysics::blockContaining(mob.motion.position);
    const QcVillage *home = context.world.villages().at(at);
    if (!home) {
        home = context.world.villages().nearest(at, homeReach);
    }
    if (!home) {
        return;
    }

    // A stroll that would lead out of the village (or a golem standing outside it) turns towards
    // its centre instead.
    const QcMobPath &path = mob.path;
    const bool strolling = path.purpose == QcMobPath::Purpose::Wander && !path.finished();
    const bool leaves = strolling && !home->contains(QcPhysics::blockContaining(path.goal));
    const bool outside = !home->contains(at) && !strolling;
    if (leaves || outside) {
        const QcVec3 centre{home->centre.x + 0.5, static_cast<double>(home->centre.y), home->centre.z + 0.5};
        mob.path.clear();
        QcMobNavigator::setGoal(mob, QcMobPath::Purpose::Wander, centre, 0);
        // Long enough to walk there: the next stroll does not cut the way back short.
        mob.wanderTimer = std::max(mob.wanderTimer, 2.0 + (centre - mob.motion.position).length() / std::max(0.1, walkSpeed));
    }
}

const QcMobManager::PlayerView *QcGolemBrain::enemy(const QcCreature &mob, const QcMobDefinition &definition, const Context &context)
{
    if (!mob.golem || mob.golem->playerBuilt || !context.players) {
        return nullptr;
    }

    const QcVillage *village = context.world.villages().at(QcPhysics::blockContaining(mob.motion.position));
    const auto hostileTo = [&](const QcMobManager::PlayerView &player) {
        const bool angry = mob.golem->angryAt == player.entityId && context.worldTime < mob.golem->angryUntil;
        const bool grudge = village && village->grudges.until.value(player.name, -1.0) > context.worldTime;
        return player.alive && player.targetable && (angry || grudge) && (player.position - mob.motion.position).length() < definition.sightRange;
    };

    const QcMobManager::PlayerView *nearest = nullptr;
    double nearestDistance = definition.sightRange;
    for (const QcMobManager::PlayerView &player : *context.players) {
        const double distance = (player.position - mob.motion.position).length();
        if (hostileTo(player) && distance < nearestDistance) {
            nearest = &player;
            nearestDistance = distance;
        }
    }
    return nearest;
}

const QcCreature *QcGolemBrain::prey(QcCreature &mob, const QcMobDefinition &definition, const Context &context, bool looks)
{
    if (!mob.golem) {
        return nullptr;
    }

    const QcContentRegistry &content = context.world.content();
    const auto current = std::ranges::find(context.mobs, mob.golem->prey, &QcCreature::id);
    if (mob.golem->prey != 0 && current != context.mobs.end() && isPrey(mob, definition, *current, content)) {
        return &*current;
    }
    mob.golem->prey = 0;
    if (!looks) {
        return nullptr;
    }

    // The nearest it sees from its eyes (one ray to the middle of the other's body).
    const QcCreature *nearest = nullptr;
    double nearestDistance = definition.sightRange;
    for (const QcCreature &other : context.mobs) {
        const double distance = (other.motion.position - mob.motion.position).length();
        if (distance >= nearestDistance || !isPrey(mob, definition, other, content)) {
            continue;
        }
        const QcVec3 middle = other.motion.position + QcVec3{0.0, content.mobs()[other.type].height * 0.5, 0.0};
        if (QcLineOfSight::seesPoint(context.world, eyeOf(mob, definition), middle)) {
            nearest = &other;
            nearestDistance = distance;
        }
    }
    mob.golem->prey = nearest ? nearest->id : 0;
    return nearest;
}

QcPhysics::Steering QcGolemBrain::pursue(QcCreature &mob, const QcMobDefinition &definition, const QcVec3 &target, const Context &context,
                                        bool mayPlan)
{
    QcMobNavigator::setGoal(mob, QcMobPath::Purpose::Chase, target, 1);
    const QcPhysics::Steering steering = navigate(mob, definition.chaseSpeed, context, mayPlan).steering;
    const QcVec3 towards{target.x - mob.motion.position.x, 0.0, target.z - mob.motion.position.z};
    const QcVec3 facing = steering.wish.length() > 0.0 ? steering.wish : towards;
    if (facing.length() > 0.0) {
        mob.yaw = QcMobSteering::yawTowards(facing);
    }
    mob.lastSpeed = steering.speed;
    return steering;
}

bool QcGolemBrain::inReach(const QcCreature &mob, const QcMobDefinition &definition, const QcVec3 &position, QcPhysics::BodySize body,
                           const QcBlockAccess &world)
{
    if (horizontalDistance(position, mob.motion.position) > definition.attackRange + body.width * 0.5
        || std::abs(position.y - mob.motion.position.y) > QcMobSteering::verticalReach) {
        return false;
    }
    return QcLineOfSight::reaches(world, eyeOf(mob, definition), QcPhysics::boxAt(position, body.width, body.height));
}

bool QcGolemBrain::readyToStrike(QcCreature &mob, const QcMobDefinition &definition, const QcVec3 &target)
{
    const QcVec3 towards{target.x - mob.motion.position.x, 0.0, target.z - mob.motion.position.z};
    if (towards.length() > 0.0) {
        mob.yaw = QcMobSteering::yawTowards(towards);
    }
    mob.path.clear();
    if (mob.attackCooldown > 0.0) {
        return false;
    }
    mob.attackCooldown = definition.attackCooldown;
    mob.attackedThisTick = true;
    return true;
}
