#include "server/pathing/QcDoorPassage.h"

#include "common/content/registry/QcContentRegistry.h"
#include "server/entities/QcCreature.h"
#include "server/mobs/QcMobSteering.h"

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

QcVec3 centreOf(QcBlockPos cell)
{
    return {cell.x + 0.5, double(cell.y), cell.z + 0.5};
}

double horizontalDistance(const QcVec3 &a, const QcVec3 &b)
{
    return std::hypot(a.x - b.x, a.z - b.z);
}

QcBlockPos feetOf(const QcCreature &mob)
{
    return QcPhysics::blockContaining(mob.motion.position + QcVec3{0.0, 0.1, 0.0});
}

} // namespace

QcDoorPassage::QcDoorPassage(const QcContentRegistry &content)
    : m_content(content)
{
}

void QcDoorPassage::startTick()
{
    m_doorways = std::exchange(m_nextDoorways, {});
}

std::optional<QcBlockPos> QcDoorPassage::doorAhead(const QcCreature &mob, const QcMobNavigator::Navigation &navigation,
                                                   const QcBlockAccess &world, bool &inside) const
{
    const auto isDoor = [&](QcBlockPos cell) {
        const std::optional<QcBlockId> block = world.blockAt(cell);
        const QcBlockDefinition *definition = block ? &m_content.block(*block) : nullptr;
        return definition && definition->toggles && definition->property(u"open"_s);
    };
    const QcBlockPos feet = feetOf(mob);
    inside = isDoor(feet);
    if (inside) {
        return feet;
    }
    if (navigation.door) {
        return navigation.door;
    }
    if (mob.path.next < mob.path.cells.size() && isDoor(mob.path.cells[mob.path.next])) {
        return mob.path.cells[mob.path.next];
    }
    return std::nullopt;
}

std::optional<QcPhysics::Steering> QcDoorPassage::waitAtDoor(QcCreature &mob, const QcMobNavigator::Navigation &navigation, const Ground &ground)
{
    bool inside = false;
    const std::optional<QcBlockPos> door = doorAhead(mob, navigation, ground.world, inside);
    const double distance = inside ? 0.0 : door ? horizontalDistance(mob.motion.position, centreOf(*door)) : queue;
    if (!door || distance >= queue || mob.fleeTimer > 0.0) {
        return std::nullopt;
    }

    // The turn of the next tick: one in the doorway first, then the one going through now while it
    // is on its way, then the nearest.
    const auto first = m_doorways.constFind(*door);
    const std::optional<Passer> going = first != m_doorways.constEnd() ? std::optional(*first) : std::nullopt;
    const auto rank = [&going](Passer passer) { return passer.distance <= 0.0 ? 0 : going && going->id == passer.id ? 1 : 2; };
    const Passer self{mob.id, distance};
    const auto next = m_nextDoorways.find(*door);
    if (next == m_nextDoorways.end() || rank(self) < rank(*next) || (rank(self) == rank(*next) && self.before(*next))) {
        m_nextDoorways.insert(*door, self);
    }
    // Its turn, or it stands in the doorway and the one going was not yet.
    if (!going || going->id == mob.id || (inside && going->distance > 0.0)) {
        return std::nullopt;
    }
    return stepAside(mob, centreOf(*door), navigation.steering.speed, ground);
}

QcPhysics::Steering QcDoorPassage::stepAside(QcCreature &mob, const QcVec3 &doorCentre, double speed, const Ground &ground)
{
    // Out of the doorway and aside, out of the way of the one coming through, facing the door.
    const QcVec3 away{mob.motion.position.x - doorCentre.x, 0.0, mob.motion.position.z - doorCentre.z};
    if (away.length() <= 0.0) {
        return {{}, speed};
    }
    mob.yaw = QcMobSteering::yawTowards(away * -1.0);
    if (away.length() >= stepBack) {
        return {{}, speed};
    }

    const QcVec3 back = away.normalized();
    const QcVec3 side = QcVec3{-back.z, 0.0, back.x} * (mob.id % 2 == 0 ? 1.0 : -1.0);
    for (const QcVec3 &step : {(back + side).normalized(), (back - side).normalized(), back}) {
        if (QcMobNavigator::safeStep(mob, step, ground.world, ground.cells, ground.rules)) {
            return {step, speed};
        }
    }
    return {{}, speed};
}

QcDoorPassage::Moves QcDoorPassage::use(const QcCreature &mob, std::optional<QcBlockPos> &opened, const QcMobNavigator::Navigation &navigation)
{
    Moves moves;
    const QcBlockPos feet = feetOf(mob);
    if (opened) {
        const QcBlockPos door = *opened;
        const bool inside = feet.x == door.x && feet.z == door.z;
        if (!inside && horizontalDistance(mob.motion.position, centreOf(door)) > passed) {
            moves.close = door;
            opened.reset();
        }
    }
    // A closed door on the path is opened, also the one it opened itself and someone shut again.
    if (navigation.door && horizontalDistance(mob.motion.position, centreOf(*navigation.door)) < reach) {
        if (opened && opened != navigation.door) {
            moves.close = *opened;
        }
        moves.open = navigation.door;
        opened = navigation.door;
    }
    return moves;
}
