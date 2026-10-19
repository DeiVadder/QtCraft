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

// Legs along the roads of a villager's village (QcMobLegs); none without one. The plan is looked up
// only when a leg is asked for.
class VillageRoutes : public QcMobLegs::Routes
{
public:
    VillageRoutes(QcVillagerRoads &roads, QcServerWorld &world, qint64 village)
        : m_roads(roads)
        , m_world(world)
        , m_village(village)
    {
    }

    int reach() const override { return QcVillagerRoads::legReach; }
    std::optional<QcBlockPos> nextLeg(QcBlockPos from, QcBlockPos to) const override
    {
        const std::shared_ptr<const QcVillagePlan> plan = planOf();
        return plan ? m_roads.nextLeg(*plan, from, to) : std::nullopt;
    }
    std::optional<QcBlockPos> wayOutOfWater(QcBlockPos from) const override
    {
        const std::shared_ptr<const QcVillagePlan> plan = planOf();
        return plan ? m_roads.wayOutOfWater(*plan, from) : std::nullopt;
    }

private:
    std::shared_ptr<const QcVillagePlan> planOf() const { return m_village != 0 ? m_world.villages().planOf(m_village) : nullptr; }

    QcVillagerRoads &m_roads;
    QcServerWorld &m_world;
    qint64 m_village = 0;
};

} // namespace

const QcVillagerTrades &QcVillagerBehaviour::trades(int mobType) const
{
    const auto it = m_trades.constFind(m_content->villagerSettingsOf(mobType).name);
    return it == m_trades.constEnd() ? m_noTrades : *it;
}

QcVillagerBehaviour::QcVillagerBehaviour(std::shared_ptr<const QcContentRegistry> content, QRandomGenerator &random, QcPathPlanner &planner)
    : m_content(std::move(content))
    , m_random(random)
    , m_planner(planner)
    , m_doors(*m_content)
{
    const QcContentRegistry &registry = *m_content;
    for (const QcVillagerSettings &species : registry.villagerSpecies()) {
        m_trades.insert(species.name, QcVillagerTrades(registry, species));
    }
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
    m_doors.startTick();
}

bool QcVillagerBehaviour::isRipe(QcBlockId block) const
{
    return block < m_ripe.size() && m_ripe[block];
}

bool QcVillagerBehaviour::rest(QcCreature &mob, const Context &context)
{
    QcVillager &villager = *mob.villager;
    const double phase = QcDayCycle::phase(context.worldTime);
    const bool night = m_content->villagerSettingsOf(mob.type).activityAt(phase, villager.profession) == QcVillagerActivity::Sleep;
    const bool inBed = villager.home && QcVillagerHomes::lyingIn(context.world, *villager.home);
    // Asleep it still keeps an eye out at its usual pace: a hostile creature nearby wakes it in a
    // panic (decision E-470).
    if (night && inBed && mob.pose == QcPose::Lying && --villager.decideIn <= 0) {
        villager.decideIn = decideTicks;
        lookOut(mob, context);
    }
    // A fresh hit or danger wakes it: the push lasts the fixed knockback ticks, whatever the game
    // rule mobInvulnerabilityTicks (decision E-429).
    if (night && inBed && mob.pose == QcPose::Lying && !mob.hurt.knockedBack() && !villager.threat) {
        mob.motion.velocity = {};
        villager.sleptAt = context.worldTime;
        return true;
    }

    // Up: beside the bed, deciding at once what to do.
    mob.pose = QcPose::Standing;
    villager.decideIn = 0;
    villager.target.reset();
    if (villager.home) {
        if (const std::optional<QcVec3> beside = QcVillagerHomes::besideBed(context.world, m_planner.cells(), *villager.home, context.rules)) {
            mob.motion.position = *beside;
        }
    }
    return false;
}

QcPhysics::Steering QcVillagerBehaviour::think(QcCreature &mob, const QcMobDefinition &definition, const Context &context, int /*thinkTicks*/)
{
    QcVillager &villager = *mob.villager;
    if (villager.trader != 0 && trade(mob, context)) {
        mob.lastSpeed = definition.walkSpeed;
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
    mob.lastSpeed = steering.speed;
    return steering;
}

bool QcVillagerBehaviour::trade(QcCreature &mob, const Context &context)
{
    QcVillager &villager = *mob.villager;
    // It keeps an eye out for danger at its usual pace; danger, a hit or bedtime ends the trade (the
    // server closes the screen when the trader is gone).
    if (--villager.decideIn <= 0) {
        villager.decideIn = decideTicks;
        lookOut(mob, context);
    }
    const double phase = QcDayCycle::phase(context.worldTime);
    const bool bedtime = m_content->villagerSettingsOf(mob.type).activityAt(phase, villager.profession) == QcVillagerActivity::Sleep;
    if (mob.fleeTimer > 0.0 || bedtime) {
        villager.trader = 0;
        villager.decideIn = 0;
        return false;
    }

    villager.target.reset();
    villager.legs.leg.reset();
    mob.path.clear();
    const QcVec3 towards{villager.traderAt.x - mob.motion.position.x, 0.0, villager.traderAt.z - mob.motion.position.z};
    if (towards.length() > 0.0) {
        mob.yaw = QcMobSteering::yawTowards(towards);
    }
    return true;
}

void QcVillagerBehaviour::leave(const QcCreature &mob)
{
    if (mob.villager->openedDoor) {
        m_actions.append({Action::Kind::CloseDoor, *mob.villager->openedDoor, mob.id});
    }
}

void QcVillagerBehaviour::decide(QcCreature &mob, const Context &context)
{
    QcVillager &villager = *mob.villager;
    const QcVillagerSettings &settings = m_content->villagerSettingsOf(mob.type);
    QcVillage *village = villager.village != 0 ? context.world.villages().find(villager.village) : nullptr;
    if (village) {
        const auto before = std::pair(villager.home, villager.work);
        QcVillagerHomes::keepClaims(*village, villager, mob.uid, settings);
        if (!villager.home) {
            QcVillagerHomes::claimBed(*village, villager, mob.uid, mob.motion.position);
        }
        // Children have no profession until they grow up (decision E-415).
        if (!villager.work && !mob.isYoung()) {
            QcVillagerHomes::claimWorkstation(*village, villager, mob.uid, settings, villager.home ? centreOf(*villager.home) : mob.motion.position);
        }
        if (before != std::pair(villager.home, villager.work)) {
            context.world.villages().noteChanged();
        }
    }
    lookOut(mob, context);

    // Looked up once per decision: the routine and the crops follow it.
    const QcVillagerProfession *profession = settings.profession(villager.profession);
    QcVillagerActivity activity = settings.activityAt(QcDayCycle::phase(context.worldTime), profession);
    // Children play instead of working.
    if (mob.isYoung() && activity == QcVillagerActivity::Work) {
        activity = QcVillagerActivity::Wander;
    }
    const bool outdoors = activity == QcVillagerActivity::Wander || activity == QcVillagerActivity::Work || activity == QcVillagerActivity::Meet;
    if (village && outdoors && settings.shelterFromRain && m_rains && m_rains(village->centre)) {
        activity = QcVillagerActivity::Home;
    }
    villager.activity = activity;

    lookForCrops(mob, profession, context);

    // A stroll goes on until it ends; every other goal follows the routine.
    if (villager.strolling && villager.target && activity == QcVillagerActivity::Wander) {
        return;
    }
    const std::optional<Goal> goal = goalOf(mob, context);
    const std::optional<QcBlockPos> target = goal ? std::optional<QcBlockPos>(goal->cell) : std::nullopt;
    if (target != villager.target) {
        villager.legs.leg.reset();
        villager.legs.retryIn = 0;
        villager.arrived = false;
    }
    villager.target = target;
    villager.closeEnough = goal ? goal->closeEnough : 1;
    villager.strolling = goal && goal->purpose == QcMobPath::Purpose::Wander;
}

void QcVillagerBehaviour::lookForCrops(QcCreature &mob, const QcVillagerProfession *profession, const Context &context) const
{
    // Farmers that may not change the world (game rule mobGriefing off, confined; QcCreature::
    // mayGrief) leave crops alone (QcServerVillagers).
    QcVillager &villager = *mob.villager;
    if (villager.activity != QcVillagerActivity::Work || !profession || !profession->tendsCrops || !mob.mayGrief(context.griefing)) {
        villager.crop.reset();
    } else if (!villager.crop && --villager.cropLookIn <= 0) {
        villager.crop = ripeCropNear(villager, context);
        villager.cropLookIn = villager.crop ? 0 : cropLookDecisions;
    }
}

void QcVillagerBehaviour::lookOut(QcCreature &mob, const Context &context)
{
    if (const std::optional<QcVec3> threat = nearestHostile(mob.motion.position, m_content->villagerSettingsOf(mob.type).fleeDistance, context)) {
        mob.villager->threat = threat;
        mob.villager->panickedAt = context.worldTime;
        mob.fleeTimer = std::max(mob.fleeTimer, fleeSeconds);
    } else if (mob.fleeTimer <= 0.0) {
        mob.villager->threat.reset();
    }
}

std::optional<QcVec3> QcVillagerBehaviour::nearestHostile(const QcVec3 &at, double fleeDistance, const Context &context)
{
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
    const QcVillager &villager = *mob.villager;
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
    const QcVillager &villager = *mob.villager;
    const QcVillage *village = villager.village != 0 ? context.world.villages().find(villager.village) : nullptr;
    const QcVec3 around = villager.home ? centreOf(*villager.home) : village ? centreOf(village->centre) : mob.motion.position;
    const double angle = m_random.bounded(2.0 * std::numbers::pi);
    const double distance = 2.0 + m_random.bounded(strollDistance - 2.0);
    const QcVec3 spot = around + QcVec3{std::sin(angle) * distance, 0.0, -std::cos(angle) * distance};
    return Goal{QcPhysics::blockContaining(spot), 1, QcMobPath::Purpose::Wander};
}

std::optional<QcBlockPos> QcVillagerBehaviour::ripeCropNear(const QcVillager &villager, const Context &context) const
{
    if (!villager.work) {
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
            if (block && isRipe(*block)) {
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
    QcVillager &villager = *mob.villager;
    // Children run about (decision E-415).
    double speed = definition.walkSpeed * (mob.isYoung() ? m_content->villagerSettingsOf(mob.type).breeding.youngSpeed : 1.0);
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
    if (const std::optional<QcPhysics::Steering> waiting = m_doors.waitAtDoor(mob, navigation, {context.world, m_planner.cells(), context.rules})) {
        return *waiting;
    }
    const QcDoorPassage::Moves doors = QcDoorPassage::use(mob, villager.openedDoor, navigation);
    villager.openedDoor = doors.opened;
    if (doors.close) {
        m_actions.append({Action::Kind::CloseDoor, *doors.close, mob.id});
    }
    if (doors.open) {
        m_actions.append({Action::Kind::OpenDoor, *doors.open, mob.id});
    }
    if (navigation.steering.wish.length() > 0.0) {
        mob.yaw = QcMobSteering::yawTowards(navigation.steering.wish);
    }
    return navigation.steering;
}

QcBlockPos QcVillagerBehaviour::legTowards(QcCreature &mob, QcBlockPos target, const Context &context)
{
    QcVillager &villager = *mob.villager;
    const VillageRoutes routes(m_roads, context.world, villager.village);
    const QcMobLegs::Towards towards = villager.legs.towards(mob, target, routes, {m_routes, m_limits.routesPerTick});
    m_routes += towards.asked;
    return towards.cell;
}

void QcVillagerBehaviour::arrive(QcCreature &mob, const Context &context)
{
    QcVillager &villager = *mob.villager;
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
            trades(mob.type).restock(villager, context.worldTime);
        }
    }
}

void QcVillagerBehaviour::lieDown(QcCreature &mob, const Context &context)
{
    QcVillager &villager = *mob.villager;
    const std::optional<QcVillagerHomes::Lying> lying =
        villager.home ? QcVillagerHomes::lyingIn(context.world, *villager.home) : std::nullopt;
    if (!lying) {
        return;
    }
    if (villager.openedDoor) {
        m_actions.append({Action::Kind::CloseDoor, *villager.openedDoor, mob.id});
        villager.openedDoor.reset();
    }
    mob.pose = QcPose::Lying;
    villager.sleptAt = context.worldTime;
    villager.legs.leg.reset();
    mob.path.clear();
    mob.motion.position = lying->position;
    mob.motion.velocity = {};
    mob.yaw = lying->yaw;
}
