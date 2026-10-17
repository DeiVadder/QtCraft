#include "server/villages/QcVillagerBehaviour.h"

#include "common/world/QcDayCycle.h"
#include "server/core/QcServerWorld.h"
#include "server/mobs/QcMobSteering.h"
#include "server/pathing/QcPathPlanner.h"
#include "worldgen/villages/QcVillagePlan.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

using namespace Qt::StringLiterals;

namespace {

// A villager hurt or near a hostile creature runs this long (seconds) after the last sign of it.
constexpr double fleeSeconds = 3.0;
// A door counts as passed once the villager is this far from its cell (horizontally).
constexpr double doorPassed = 1.5;
// Villagers this close to a door on their way take turns at it; one that waits steps back to this
// distance.
constexpr double doorQueue = 2.5;
constexpr double doorStepBack = 1.8;
// A leg counts as walked this close to its cell.
constexpr double legArrived = 2.0;
// Share of strolling decisions that stand still instead.
constexpr double standChance = 0.4;

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

QcVillagerBehaviour::QcVillagerBehaviour(std::shared_ptr<const QcContentRegistry> content, QRandomGenerator &random, QcPathPlanner &planner)
    : m_content(std::move(content))
    , m_random(random)
    , m_planner(planner)
    , m_trades(*m_content)
{
    const QcContentRegistry &registry = *m_content;
    m_ripe.assign(std::size_t(registry.stateCount()), false);
    for (qsizetype id = 0; id < registry.stateCount(); ++id) {
        const QcBlockDefinition &block = registry.block(QcBlockId(id));
        const QcBlockProperty *stages = block.crop.isCrop() ? block.property(block.crop.property) : nullptr;
        m_ripe[std::size_t(id)] = stages && !stages->values.isEmpty() && registry.property(QcBlockId(id), block.crop.property) == stages->values.constLast();
    }
}

void QcVillagerBehaviour::startTick()
{
    m_thoughts = 0;
    m_routes = 0;
    m_harvests = 0;
    m_hostilesGathered = false;
    m_doorways = std::exchange(m_nextDoorways, {});
}

bool QcVillagerBehaviour::isRipe(QcBlockId block) const
{
    return block < m_ripe.size() && m_ripe[block];
}

std::shared_ptr<const QcVillagePlan> QcVillagerBehaviour::planOf(const QcVillager &villager, const Context &context) const
{
    return villager.village != 0 ? context.world.villages().planOf(villager.village) : nullptr;
}

bool QcVillagerBehaviour::rest(QcCreature &mob, const Context &context)
{
    QcVillager &villager = mob.villager;
    const bool night = m_content->villagerSettings().activityAt(QcDayCycle::phase(context.worldTime)) == QcVillagerActivity::Sleep;
    const bool inBed = villager.home && QcVillagerHomes::lyingIn(context.world, *villager.home);
    if (night && inBed && mob.hurtTimer <= 0.0) {
        mob.motion.velocity = {};
        return true;
    }

    // Up: beside the bed, deciding at once what to do.
    villager.sleeping = false;
    villager.decideIn = 0;
    villager.target.reset();
    if (villager.home) {
        if (const std::optional<QcVec3> beside = QcVillagerHomes::besideBed(context.world, m_planner.cells(), *villager.home, context.rules)) {
            mob.motion.position = *beside;
        }
    }
    return false;
}

QcPhysics::Steering QcVillagerBehaviour::think(QcCreature &mob, const QcMobDefinition &definition, const Context &context)
{
    QcVillager &villager = mob.villager;
    if (villager.trader != 0 && trade(mob, context)) {
        return {{}, definition.walkSpeed};
    }

    --villager.decideIn;
    if (villager.decideIn <= 0 && m_thoughts < m_limits.thoughtsPerTick) {
        ++m_thoughts;
        villager.decideIn = decideTicks;
        decide(mob, context);
    }

    const QcPhysics::Steering steering = walk(mob, definition, context);
    arrive(mob, context);
    return steering;
}

bool QcVillagerBehaviour::trade(QcCreature &mob, const Context &context)
{
    QcVillager &villager = mob.villager;
    // It keeps an eye out for danger at its usual pace; danger, a hit or bedtime ends the trade (the
    // server closes the screen when the trader is gone).
    if (--villager.decideIn <= 0) {
        villager.decideIn = decideTicks;
        lookOut(mob, context);
    }
    const bool bedtime = m_content->villagerSettings().activityAt(QcDayCycle::phase(context.worldTime)) == QcVillagerActivity::Sleep;
    if (mob.fleeTimer > 0.0 || bedtime) {
        villager.trader = 0;
        villager.decideIn = 0;
        return false;
    }

    villager.target.reset();
    villager.leg.reset();
    mob.path.clear();
    const QcVec3 towards{villager.traderAt.x - mob.motion.position.x, 0.0, villager.traderAt.z - mob.motion.position.z};
    if (towards.length() > 0.0) {
        mob.yaw = QcMobSteering::yawTowards(towards);
    }
    return true;
}

void QcVillagerBehaviour::leave(const QcCreature &mob)
{
    if (mob.villager.openedDoor) {
        m_actions.append({Action::Kind::CloseDoor, *mob.villager.openedDoor, mob.id});
    }
}

void QcVillagerBehaviour::decide(QcCreature &mob, const Context &context)
{
    QcVillager &villager = mob.villager;
    const QcVillagerSettings &settings = m_content->villagerSettings();
    QcVillage *village = villager.village != 0 ? context.world.villages().find(villager.village) : nullptr;
    if (village) {
        const auto before = std::pair(villager.home, villager.work);
        QcVillagerHomes::keepClaims(*village, villager, settings);
        if (!villager.home) {
            QcVillagerHomes::claimBed(*village, villager, mob.motion.position);
        }
        // Children have no profession until they grow up (decision E-415).
        if (!villager.work && !mob.isYoung()) {
            QcVillagerHomes::claimWorkstation(*village, villager, settings, villager.home ? centreOf(*villager.home) : mob.motion.position);
        }
        if (before != std::pair(villager.home, villager.work)) {
            context.world.villages().noteChanged();
        }
    }
    lookOut(mob, context);

    QcVillagerActivity activity = settings.activityAt(QcDayCycle::phase(context.worldTime));
    // Children play instead of working.
    if (mob.isYoung() && activity == QcVillagerActivity::Work) {
        activity = QcVillagerActivity::Wander;
    }
    const bool outdoors = activity == QcVillagerActivity::Wander || activity == QcVillagerActivity::Work || activity == QcVillagerActivity::Meet;
    if (village && outdoors && settings.shelterFromRain && m_rains && m_rains(village->centre)) {
        activity = QcVillagerActivity::Home;
    }
    villager.activity = activity;

    lookForCrops(villager, context);

    // A stroll goes on until it ends; every other goal follows the routine.
    if (villager.strolling && villager.target && activity == QcVillagerActivity::Wander) {
        return;
    }
    const std::optional<Goal> goal = goalOf(mob, context);
    const std::optional<QcBlockPos> target = goal ? std::optional<QcBlockPos>(goal->cell) : std::nullopt;
    if (target != villager.target) {
        villager.leg.reset();
        villager.legRetryIn = 0;
        villager.arrived = false;
    }
    villager.target = target;
    villager.closeEnough = goal ? goal->closeEnough : 1;
    villager.strolling = goal && goal->purpose == QcMobPath::Purpose::Wander;
}

void QcVillagerBehaviour::lookForCrops(QcVillager &villager, const Context &context) const
{
    const QcVillagerProfession *profession = m_content->villagerSettings().profession(villager.profession);
    if (villager.activity != QcVillagerActivity::Work || !profession || !profession->tendsCrops) {
        villager.crop.reset();
    } else if (!villager.crop && --villager.cropLookIn <= 0) {
        villager.crop = ripeCropNear(villager, context);
        villager.cropLookIn = villager.crop ? 0 : cropLookDecisions;
    }
}

void QcVillagerBehaviour::lookOut(QcCreature &mob, const Context &context)
{
    if (const std::optional<QcVec3> threat = nearestHostile(mob.motion.position, context)) {
        mob.villager.threat = threat;
        mob.fleeTimer = std::max(mob.fleeTimer, fleeSeconds);
    } else if (mob.fleeTimer <= 0.0) {
        mob.villager.threat.reset();
    }
}

std::optional<QcVec3> QcVillagerBehaviour::nearestHostile(const QcVec3 &at, const Context &context)
{
    const double fleeDistance = m_content->villagerSettings().fleeDistance;
    const QHash<QcChunkPos, QList<QcVec3>> &hostiles = this->hostiles(context);
    const QcChunkPos from = QcPhysics::blockContaining(at - QcVec3{fleeDistance, 0.0, fleeDistance}).chunk();
    const QcChunkPos to = QcPhysics::blockContaining(at + QcVec3{fleeDistance, 0.0, fleeDistance}).chunk();
    double nearest = fleeDistance;
    std::optional<QcVec3> threat;
    const auto scan = [&](const QList<QcVec3> &column) {
        for (const QcVec3 &other : column) {
            if (const double distance = (other - at).length(); distance < nearest) {
                nearest = distance;
                threat = other;
            }
        }
    };
    for (int x = from.x; x <= to.x; ++x) {
        for (int z = from.z; z <= to.z; ++z) {
            scan(hostiles.value({x, z}));
        }
    }
    return threat;
}

const QHash<QcChunkPos, QList<QcVec3>> &QcVillagerBehaviour::hostiles(const Context &context)
{
    if (m_hostilesGathered) {
        return m_hostiles;
    }
    m_hostilesGathered = true;
    m_hostiles.clear();
    for (const QcCreature &other : context.mobs) {
        if (!other.dormant && m_content->mobs()[other.type].hostile) {
            m_hostiles[QcPhysics::blockContaining(other.motion.position).chunk()].append(other.motion.position);
        }
    }
    return m_hostiles;
}

std::optional<QcVillagerBehaviour::Goal> QcVillagerBehaviour::goalOf(QcCreature &mob, const Context &context)
{
    const QcVillager &villager = mob.villager;
    const QcVillage *village = villager.village != 0 ? context.world.villages().find(villager.village) : nullptr;
    switch (villager.activity) {
    case QcVillagerActivity::Sleep:
    case QcVillagerActivity::Home:
        if (villager.home) {
            return Goal{*villager.home, 1};
        }
        break;
    case QcVillagerActivity::Work:
        if (villager.crop) {
            return Goal{*villager.crop, 1};
        }
        if (villager.work) {
            return Goal{*villager.work, 1};
        }
        break;
    case QcVillagerActivity::Meet:
        if (village) {
            return Goal{village->centre, meetCloseEnough};
        }
        break;
    case QcVillagerActivity::Wander:
        break;
    }
    return strollGoal(mob, context);
}

std::optional<QcVillagerBehaviour::Goal> QcVillagerBehaviour::strollGoal(const QcCreature &mob, const Context &context)
{
    if (m_random.generateDouble() < standChance) {
        return std::nullopt;
    }
    const QcVillager &villager = mob.villager;
    const QcVillage *village = villager.village != 0 ? context.world.villages().find(villager.village) : nullptr;
    const QcVec3 around = villager.home ? centreOf(*villager.home) : village ? centreOf(village->centre) : mob.motion.position;
    const double angle = m_random.bounded(2.0 * std::numbers::pi);
    const double distance = 2.0 + m_random.bounded(strollDistance - 2.0);
    const QcVec3 spot = around + QcVec3{std::sin(angle) * distance, 0.0, -std::cos(angle) * distance};
    return Goal{QcPhysics::blockContaining(spot), 1, QcMobPath::Purpose::Wander};
}

std::optional<QcBlockPos> QcVillagerBehaviour::ripeCropNear(const QcVillager &villager, const Context &context) const
{
    const std::shared_ptr<const QcVillagePlan> plan = planOf(villager, context);
    if (!villager.work || !plan || plan->fields.isEmpty()) {
        return std::nullopt;
    }
    const QcBlockPos work = *villager.work;
    std::optional<QcBlockPos> best;
    int bestDistance = std::numeric_limits<int>::max();
    // A ripe crop in the column at `dx`, `dz` from the workstation, from two below to one above it.
    const auto ripeIn = [&](int dx, int dz) -> std::optional<QcBlockPos> {
        for (int dy = -2; dy <= 1; ++dy) {
            const QcBlockPos pos = work.offset(dx, dy, dz);
            const std::optional<QcBlockId> block = context.world.blockAt(pos);
            if (block && isRipe(*block) && plan->fields.contains(pos)) {
                return pos;
            }
        }
        return std::nullopt;
    };

    for (int dz = -cropRadius; dz <= cropRadius; ++dz) {
        for (int dx = -cropRadius; dx <= cropRadius; ++dx) {
            const int distance = dx * dx + dz * dz;
            const std::optional<QcBlockPos> ripe = distance < bestDistance ? ripeIn(dx, dz) : std::nullopt;
            if (ripe) {
                best = ripe;
                bestDistance = distance;
            }
        }
    }
    return best;
}

QcPhysics::Steering QcVillagerBehaviour::walk(QcCreature &mob, const QcMobDefinition &definition, const Context &context)
{
    QcVillager &villager = mob.villager;
    // Children run about (decision E-415).
    double speed = definition.walkSpeed * (mob.isYoung() ? m_content->villagerSettings().breeding.youngSpeed : 1.0);
    if (mob.fleeTimer > 0.0) {
        // Home when it has one and is not there yet, otherwise away from the danger.
        speed = definition.chaseSpeed;
        if (villager.home && horizontalDistance(mob.motion.position, centreOf(*villager.home)) > reach) {
            const QcBlockPos cell = legTowards(mob, *villager.home, context);
            QcMobNavigator::setGoal(mob, QcMobPath::Purpose::Flee, centreOf(cell), 1);
        } else if (!villager.home) {
            const QcVec3 away = villager.threat ? mob.motion.position - *villager.threat : mob.wanderDirection;
            const QcVec3 flat{away.x, 0.0, away.z};
            if (flat.length() > 0.0) {
                QcMobNavigator::setGoal(mob, QcMobPath::Purpose::Flee, mob.motion.position + flat.normalized() * 10.0, 0);
            }
        } else {
            mob.path.clear();
        }
    } else if (!villager.target || villager.arrived) {
        mob.path.clear();
    } else {
        const QcBlockPos cell = legTowards(mob, *villager.target, context);
        const QcMobPath::Purpose purpose = villager.strolling ? QcMobPath::Purpose::Wander : QcMobPath::Purpose::Errand;
        QcMobNavigator::setGoal(mob, purpose, centreOf(cell), cell == *villager.target ? villager.closeEnough : 1);
    }

    const QcMobNavigator::Navigation navigation = QcMobNavigator::steer(mob, speed, m_planner, context.world, context.rules);
    if (const std::optional<QcPhysics::Steering> waiting = waitAtDoor(mob, navigation, context)) {
        return *waiting;
    }
    useDoors(mob, navigation);
    if (navigation.steering.wish.length() > 0.0) {
        mob.yaw = QcMobSteering::yawTowards(navigation.steering.wish);
    }
    return navigation.steering;
}

std::optional<QcBlockPos> QcVillagerBehaviour::doorAhead(const QcCreature &mob, const QcMobNavigator::Navigation &navigation,
                                                         const Context &context, bool &inside) const
{
    const auto isDoor = [&](QcBlockPos cell) {
        const std::optional<QcBlockId> block = context.world.blockAt(cell);
        const QcBlockDefinition *definition = block ? &m_content->block(*block) : nullptr;
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

std::optional<QcPhysics::Steering> QcVillagerBehaviour::waitAtDoor(QcCreature &mob, const QcMobNavigator::Navigation &navigation,
                                                                   const Context &context)
{
    bool inside = false;
    const std::optional<QcBlockPos> door = doorAhead(mob, navigation, context, inside);
    const double distance = inside ? 0.0 : door ? horizontalDistance(mob.motion.position, centreOf(*door)) : doorQueue;
    if (!door || distance >= doorQueue || mob.fleeTimer > 0.0) {
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
    return stepAside(mob, centreOf(*door), navigation.steering.speed, context);
}

QcPhysics::Steering QcVillagerBehaviour::stepAside(QcCreature &mob, const QcVec3 &doorCentre, double speed, const Context &context) const
{
    // Out of the doorway and aside, out of the way of the one coming through, facing the door.
    const QcVec3 away{mob.motion.position.x - doorCentre.x, 0.0, mob.motion.position.z - doorCentre.z};
    if (away.length() <= 0.0) {
        return {{}, speed};
    }
    mob.yaw = QcMobSteering::yawTowards(away * -1.0);
    if (away.length() >= doorStepBack) {
        return {{}, speed};
    }

    const QcVec3 back = away.normalized();
    const QcVec3 side = QcVec3{-back.z, 0.0, back.x} * (mob.id % 2 == 0 ? 1.0 : -1.0);
    for (const QcVec3 &step : {(back + side).normalized(), (back - side).normalized(), back}) {
        if (QcMobNavigator::safeStep(mob, step, context.world, m_planner.cells(), context.rules)) {
            return {step, speed};
        }
    }
    return {{}, speed};
}

QcBlockPos QcVillagerBehaviour::legTowards(QcCreature &mob, QcBlockPos target, const Context &context)
{
    QcVillager &villager = mob.villager;
    if (const std::optional<QcBlockPos> shore = wayOutOfWater(mob, context)) {
        return *shore;
    }
    const QcBlockPos feet = feetOf(mob);
    if (std::abs(target.x - feet.x) <= QcVillagerRoads::legReach && std::abs(target.z - feet.z) <= QcVillagerRoads::legReach) {
        villager.leg.reset();
        return target;
    }
    const bool walking = villager.leg && horizontalDistance(mob.motion.position, centreOf(*villager.leg)) > legArrived
                         && !(mob.path.finished() && mob.path.plannedGoal == *villager.leg);
    villager.legRetryIn = std::max(0, villager.legRetryIn - 1);
    if (walking || villager.legRetryIn > 0 || m_routes >= m_limits.routesPerTick) {
        return villager.leg.value_or(target);
    }

    ++m_routes;
    const std::shared_ptr<const QcVillagePlan> plan = planOf(villager, context);
    villager.leg = plan ? m_roads.nextLeg(*plan, feet, target) : std::nullopt;
    villager.legRetryIn = villager.leg ? 0 : legRetryTicks;
    return villager.leg.value_or(target);
}

std::optional<QcBlockPos> QcVillagerBehaviour::wayOutOfWater(QcCreature &mob, const Context &context)
{
    QcVillager &villager = mob.villager;
    if (!mob.motion.inWater) {
        // Ashore once it stands on dry ground (at the surface it bobs in and out of the water).
        if (mob.motion.onGround) {
            villager.shore.reset();
        }
        return villager.shore;
    }
    const bool noWay = mob.path.finished() && !mob.path.reachesGoal && !mob.path.queued;
    if (villager.shore || !noWay || m_routes >= m_limits.routesPerTick) {
        return villager.shore;
    }

    ++m_routes;
    const std::shared_ptr<const QcVillagePlan> plan = planOf(villager, context);
    villager.shore = plan ? m_roads.wayOutOfWater(*plan, feetOf(mob)) : std::nullopt;
    villager.leg.reset();
    return villager.shore;
}

void QcVillagerBehaviour::useDoors(QcCreature &mob, const QcMobNavigator::Navigation &navigation)
{
    QcVillager &villager = mob.villager;
    const QcBlockPos feet = feetOf(mob);
    if (villager.openedDoor) {
        const QcBlockPos door = *villager.openedDoor;
        const bool inside = feet.x == door.x && feet.z == door.z;
        if (!inside && horizontalDistance(mob.motion.position, centreOf(door)) > doorPassed) {
            m_actions.append({Action::Kind::CloseDoor, door, mob.id});
            villager.openedDoor.reset();
        }
    }
    // A closed door on the path is opened, also the one it opened itself and someone shut again.
    if (navigation.door && horizontalDistance(mob.motion.position, centreOf(*navigation.door)) < reach) {
        if (villager.openedDoor && villager.openedDoor != navigation.door) {
            m_actions.append({Action::Kind::CloseDoor, *villager.openedDoor, mob.id});
        }
        m_actions.append({Action::Kind::OpenDoor, *navigation.door, mob.id});
        villager.openedDoor = navigation.door;
    }
}

void QcVillagerBehaviour::arrive(QcCreature &mob, const Context &context)
{
    QcVillager &villager = mob.villager;
    if (!villager.target || mob.fleeTimer > 0.0) {
        villager.arrived = false;
        return;
    }
    const QcBlockPos target = *villager.target;
    const double distance = horizontalDistance(mob.motion.position, centreOf(target));
    const bool level = std::abs(mob.motion.position.y - target.y) <= 1.5;
    const bool there = distance <= reach && level;
    if (villager.strolling) {
        if (there || (mob.path.finished() && !mob.path.queued)) {
            villager.target.reset();
        }
        return;
    }

    // There: within reach or the target's closeEnough (the meeting), or at the end of a path that
    // gets there; it stays there while it stands a little farther (pushed by the crowd).
    const double near = std::max(reach, double(villager.closeEnough));
    const bool pathThere = mob.path.finished() && mob.path.reachesGoal && mob.path.plannedGoal == target && level;
    villager.arrived = (distance <= near && level) || pathThere || (villager.arrived && distance <= near + 1.0 && level);
    if (!villager.arrived) {
        return;
    }

    if (villager.activity == QcVillagerActivity::Sleep && target == villager.home) {
        lieDown(mob, context);
    } else if (villager.crop && target == *villager.crop) {
        if (m_harvests < m_limits.harvestsPerTick) {
            ++m_harvests;
            m_actions.append({Action::Kind::Harvest, target, mob.id});
            villager.crop.reset();
            villager.target.reset();
            villager.decideIn = std::min(villager.decideIn, 2);
        }
    } else if (target == villager.work) {
        // At work: facing the workstation, restocking its offers when due.
        const QcVec3 towards = centreOf(target) - mob.motion.position;
        if (QcVec3{towards.x, 0.0, towards.z}.length() > 0.1) {
            mob.yaw = QcMobSteering::yawTowards({towards.x, 0.0, towards.z});
        }
        if (villager.activity == QcVillagerActivity::Work) {
            m_trades.restock(villager, context.worldTime);
        }
    }
}

void QcVillagerBehaviour::lieDown(QcCreature &mob, const Context &context)
{
    QcVillager &villager = mob.villager;
    const std::optional<QcVillagerHomes::Lying> lying =
        villager.home ? QcVillagerHomes::lyingIn(context.world, *villager.home) : std::nullopt;
    if (!lying) {
        return;
    }
    if (villager.openedDoor) {
        m_actions.append({Action::Kind::CloseDoor, *villager.openedDoor, mob.id});
        villager.openedDoor.reset();
    }
    villager.sleeping = true;
    villager.leg.reset();
    mob.path.clear();
    mob.motion.position = lying->position;
    mob.motion.velocity = {};
    mob.yaw = lying->yaw;
}
