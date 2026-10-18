#include "server/mobs/QcNeutralBrain.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/physics/QcPhysics.h"
#include "server/core/QcServerWorld.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/mobs/QcMobSteering.h"
#include "server/mobs/QcMobTeleport.h"

#include <algorithm>
#include <cmath>

namespace {

QcVec3 eyeOf(const QcCreature &mob, const QcMobDefinition &definition)
{
    return mob.motion.position + QcVec3{0.0, definition.height * QcMobSteering::eyeShare, 0.0};
}

QcVec3 playerEyes(const QcMobManager::PlayerView &player)
{
    return player.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
}

const QcMobManager::PlayerView *playerWithId(const QList<QcMobManager::PlayerView> &players, quint32 id)
{
    const auto it = std::ranges::find(players, id, &QcMobManager::PlayerView::entityId);
    return it == players.end() ? nullptr : &*it;
}

bool validTarget(const QcMobManager::PlayerView &player)
{
    return player.alive && player.targetable;
}

} // namespace

QcPhysics::Steering QcNeutralBrain::think(QcCreature &mob, const QcMobDefinition &definition, const Context &context, int thinkTicks)
{
    const bool mayPlan = thinkTicks <= 1;
    const bool looks = (context.tick + mob.id) % QcMobSteering::sightCheckTicks < quint64(std::max(1, thinkTicks));
    // Water hurts a teleporter (QcBodyBurning): it leaves at once.
    if (definition.teleport && definition.teleport->whenWet && mob.motion.inWater) {
        QcMobTeleport::teleport(mob, definition, context.world, random());
    }

    if (const QcMobManager::PlayerView *player = provoker(mob, definition, context, looks)) {
        mob.provokedBy = player->entityId;
        mob.angerTimer = angerSeconds;
    }
    mob.struckBy = 0;
    const QcMobManager::PlayerView *target = angryAt(mob, context);
    mob.chasing = target ? target->entityId : 0;
    if (!target) {
        carry(mob, definition, context, thinkTicks);
        const QcPhysics::Steering steering = wander(mob, definition, context, mayPlan);
        mob.lastSpeed = steering.speed;
        return steering;
    }

    // Seeing the target keeps it angry; a teleporter far from it now and then jumps next to it.
    if (looks && QcLineOfSight::seesPoint(context.world, eyeOf(mob, definition), playerEyes(*target))) {
        mob.angerTimer = angerSeconds;
    }
    const double distance = (target->position - mob.motion.position).length();
    if (definition.teleport && distance > farTarget
        && random().generateDouble() < definition.teleport->towardsTargetChance * context.dt * thinkTicks) {
        QcMobTeleport::teleport(mob, definition, context.world, random(), target->position);
    }

    const QcPhysics::Steering steering = chase(mob, definition, *target, context, mayPlan);
    mob.lastSpeed = steering.speed;
    return steering;
}

bool QcNeutralBrain::gazedAt(const QcMobManager::PlayerView &player, const QcVec3 &head, const QcGazeDefinition &gaze,
                             const QcBlockAccess &world)
{
    const QcVec3 eyes = playerEyes(player);
    const QcVec3 towards = head - eyes;
    const double distance = towards.length();
    if (distance > gaze.range || distance < 1e-6 || player.look.length() < 1e-6) {
        return false;
    }

    // Narrower with the distance: about 4.5° at 8 m, 1.8° at 50 m with the default 0.025.
    const double cosine = player.look.normalized().dot(towards * (1.0 / distance));
    if (cosine <= 1.0 - gaze.tolerance / distance) {
        return false;
    }
    return QcLineOfSight::seesPoint(world, eyes, head);
}

const QcMobManager::PlayerView *QcNeutralBrain::provoker(const QcCreature &mob, const QcMobDefinition &definition,
                                                         const Context &context, bool looks) const
{
    const QList<QcMobManager::PlayerView> &players = *context.players;
    // A look at its head, checked in its turn like a hostile creature's look for players.
    if (looks && definition.gaze) {
        const QcVec3 head = mob.motion.position + QcVec3{0.0, definition.height * definition.gaze->headShare, 0.0};
        for (const QcMobManager::PlayerView &player : players) {
            if (validTarget(player) && gazedAt(player, head, *definition.gaze, context.world)) {
                return &player;
            }
        }
    }
    // Hurt by a player: that one.
    const QcMobManager::PlayerView *attacker = mob.struckBy != 0 ? playerWithId(players, mob.struckBy) : nullptr;
    return attacker && validTarget(*attacker) ? attacker : nullptr;
}

const QcMobManager::PlayerView *QcNeutralBrain::angryAt(QcCreature &mob, const Context &context)
{
    const QcMobManager::PlayerView *target = mob.provokedBy != 0 && mob.angerTimer > 0.0 ? playerWithId(*context.players, mob.provokedBy) : nullptr;
    if (!target || !validTarget(*target)) {
        mob.provokedBy = 0;
        mob.angerTimer = 0.0;
        return nullptr;
    }
    return target;
}

void QcNeutralBrain::carry(QcCreature &mob, const QcMobDefinition &definition, const Context &context, int thinkTicks)
{
    if (!definition.carries || mob.confined || !context.blockActions) {
        return;
    }

    const double seconds = context.dt * thinkTicks;
    if (mob.carried == qcAirBlockId) {
        if (context.griefing && random().generateDouble() < definition.carries->takeChance * seconds) {
            takeBlock(mob, definition, context);
        }
    } else if (random().generateDouble() < definition.carries->placeChance * seconds) {
        // Without the game rule it does not change the world: the block falls as an item, so the
        // creature is not kept with the world for it forever.
        if (context.griefing) {
            placeBlock(mob, definition, context);
        } else {
            context.blockActions->append({QcMobManager::BlockAction::Kind::Drop, mob.id, {}, mob.carried});
        }
    }
}

void QcNeutralBrain::takeBlock(const QcCreature &mob, const QcMobDefinition &definition, const Context &context)
{
    // A random block around its feet, from the floor up to a little above its reach, that it sees.
    const QcCarryDefinition &carry = *definition.carries; // NOLINT(bugprone-unchecked-optional-access): carry() checked it
    const int reach = carry.reach;
    const QcBlockPos feet = QcPhysics::blockContaining(mob.motion.position);
    const QcBlockPos pos = feet.offset(random().bounded(-reach, reach + 1), random().bounded(-1, reach + 2), random().bounded(-reach, reach + 1));
    const std::optional<QcBlockId> block = context.world.blockAt(pos);
    const QcContentRegistry &content = context.world.content();
    if (!block || *block == qcAirBlockId
        || std::ranges::none_of(carry.blockIds, [&](QcBlockId id) { return content.isSameBlock(*block, id); })) {
        return;
    }
    if (QcLineOfSight::reaches(context.world, eyeOf(mob, definition), QcLineOfSight::blockBox(pos), pos)) {
        context.blockActions->append({QcMobManager::BlockAction::Kind::Take, mob.id, pos, *block});
    }
}

void QcNeutralBrain::placeBlock(const QcCreature &mob, const QcMobDefinition &definition, const Context &context)
{
    const QcBlockPos feet = QcPhysics::blockContaining(mob.motion.position);
    const QcBlockPos pos = feet.offset(random().bounded(-1, 2), random().bounded(0, 3), random().bounded(-1, 2));
    const QcContentRegistry &content = context.world.content();
    const std::optional<QcBlockId> below = context.world.blockAt(pos.offset(0, -1, 0));
    if (context.world.blockAt(pos) != qcAirBlockId || !below || !content.isValidBlock(*below) || !content.block(*below).solid
        || !content.state(*below).fullCube || !QcLineOfSight::reaches(context.world, eyeOf(mob, definition), QcLineOfSight::blockBox(pos), pos)) {
        return;
    }
    context.blockActions->append({QcMobManager::BlockAction::Kind::Place, mob.id, pos, mob.carried});
}
