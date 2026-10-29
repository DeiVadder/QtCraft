#include "server/boats/QcServerBoats.h"

#include "common/core/QcWorldConstants.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcChatText.h"
#include "common/world/QcBoatBody.h"
#include "server/boats/QcBoatManager.h"
#include "server/boats/QcBoxGrid.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/gameplay/QcServerCombat.h"
#include "server/gameplay/QcServerInventory.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

const QString placeSound = u"qtcraft:block.wood.place"_s;
const QString breakSound = u"qtcraft:block.wood.break"_s;

// Whether a player standing with the feet at the bottom of `cell` fits there and stands on a block
// (not on water).
bool standsAt(const QcBlockAccess &world, QcBlockPos cell)
{
    for (const QcBlockPos pos : {cell, cell.offset(0, 1, 0)}) {
        if (!world.blockAt(pos) || !QcPhysics::collisionBoxesAt(world, pos).isEmpty() || QcPhysics::isLiquidAt(world, pos)) {
            return false;
        }
    }
    return !QcPhysics::collisionBoxesAt(world, cell.offset(0, -1, 0)).isEmpty();
}

// The horizontal axis direction nearest to `direction` as a cell offset.
QcBlockPos axisStep(const QcVec3 &direction)
{
    if (std::abs(direction.x) >= std::abs(direction.z)) {
        return {direction.x >= 0.0 ? 1 : -1, 0, 0};
    }
    return {0, 0, direction.z >= 0.0 ? 1 : -1};
}

} // namespace

QcServerBoats::QcServerBoats(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory)
    : m_state(state)
    , m_sounds(sounds)
    , m_inventory(inventory)
{
}

quint32 QcServerBoats::vehicleOf(const QcServerClient &client) const
{
    const QcBoat *boat = m_state.entities().boats().drivenBy(client.id);
    return boat ? boat->id : 0;
}

std::optional<QcBoatState> QcServerBoats::stateOf(const QcServerClient &client) const
{
    const QcBoat *boat = m_state.entities().boats().drivenBy(client.id);
    return boat ? std::optional(boat->state()) : std::nullopt;
}

bool QcServerBoats::fits(const QcCreature &creature) const
{
    const QList<QcMobDefinition> &kinds = m_state.content().mobs();
    if (creature.type < 0 || creature.type >= kinds.size()) {
        return false;
    }
    const QcMobDefinition &kind = kinds[creature.type];
    return !kind.aquatic && !kind.flying && !kind.hover && !kind.ambient && kind.width < QcBoatBody::width;
}

QcBoat *QcServerBoats::spawn(QcBoat boat)
{
    boat.id = m_state.entities().newId();
    boat.driver = 0;
    boat.passenger = 0;
    boat.yaw = qcWrappedYaw(boat.yaw);
    if (!m_state.entities().boats().add(boat)) {
        return nullptr;
    }
    return m_state.entities().boats().find(boat.id);
}

QcBoat *QcServerBoats::summon(const QString &item, const QcVec3 &wanted, float yaw)
{
    const std::optional<QcItemId> id = m_state.content().itemId(item);
    if (!id || !m_state.content().item(*id).placesBoat()) {
        return nullptr;
    }

    QcBoat boat;
    boat.item = m_state.content().item(*id).name;
    boat.motion.position = wanted;
    boat.yaw = yaw;
    return spawn(boat);
}

std::optional<QcVec3> QcServerBoats::placeFor(const QcMsg::UseItem &message) const
{
    const QcBlockAccess &world = m_state.world();
    const QcVec3 middle{message.target.x + 0.5, static_cast<double>(message.target.y), message.target.z + 0.5};
    // On water it floats at once; a column of water takes the boat to its top.
    if (QcPhysics::isWaterAt(world, message.target)) {
        QcBlockPos top = message.target;
        while (QcPhysics::isWaterAt(world, top.offset(0, 1, 0)) && top.y < message.target.y + 8) {
            top = top.offset(0, 1, 0);
        }
        const double surface = QcBoatMotion::waterSurfaceAt(world, top).value_or(top.y + 1.0 - QcBoatMotion::liquidSurfaceDrop);
        return QcVec3{middle.x, surface - QcBoatMotion::draft, middle.z};
    }
    if (message.face == QcBlockFace::Up && !QcPhysics::collisionBoxesAt(world, message.target).isEmpty()) {
        return QcVec3{middle.x, message.target.y + 1.0, middle.z};
    }
    return std::nullopt;
}

bool QcServerBoats::place(QcServerClient &client, const QcMsg::UseItem &message)
{
    const QcItemDefinition *held = m_state.heldItem(client);
    if (!held || !held->placesBoat()) {
        return false;
    }

    const std::optional<QcVec3> spot = placeFor(message);
    const auto blocked = [this, &spot] {
        const QcAabb box = QcBoatMotion::boxAt(*spot);
        return QcPhysics::overlapsBlocks(m_state.world(), box)
               || std::ranges::any_of(m_state.entities().boats().all(), [&box](const QcBoat &other) { return QcBoatManager::bodyOf(other).intersects(box); });
    };
    if (client.dead || !spot || !QcLineOfSight::isWithinReach(client, message.target) || blocked()) {
        m_inventory.send(client);
        return true;
    }

    QcBoat boat;
    boat.item = held->name;
    boat.motion.position = *spot;
    // It points where the player looks, as in the role model.
    boat.yaw = qcWrappedYaw(client.yaw);
    if (!spawn(boat)) {
        client.connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "Too many boats in the world (at most %1)"),
                                                    {QString::number(m_state.entities().boats().limit())}));
        m_inventory.send(client);
        return true;
    }

    if (!client.rules().infiniteItems) {
        client.inventory.removeFromSlot(client.inventory.selectedHotbarSlot(), 1);
    }
    m_sounds.play(placeSound, *spot);
    m_inventory.send(client);
    return true;
}

bool QcServerBoats::canReach(const QcServerClient &client, const QcBoat &boat) const
{
    const QcAabb body = QcBoatManager::bodyOf(boat);
    const QcVec3 eye = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const QcVec3 centre = (body.min + body.max) * 0.5;
    return !client.dead && (centre - eye).length() <= QcServerCombat::attackReach + 1.0 && QcLineOfSight::canSee(m_state.world(), client, body);
}

bool QcServerBoats::use(QcServerClient &client, quint32 id)
{
    QcBoat *boat = m_state.entities().boats().find(id);
    if (!boat) {
        return false;
    }
    if (boat->driver != 0 || vehicleOf(client) != 0 || m_state.entities().minecarts().ridden(client.id) || !canReach(client, *boat)) {
        return true;
    }

    boat->driver = client.id;
    boat->rowing = false;
    boat->rest.reset();
    client.mining.pos.reset();
    seatDriver(client, *boat);
    m_state.advancements().trigger(client, {.trigger = QcAdvancementTrigger::RideVehicle, .vehicle = u"boat"_s});
    return true;
}

bool QcServerBoats::hit(QcServerClient &client, quint32 id)
{
    QcBoat *boat = m_state.entities().boats().find(id);
    if (!boat) {
        return false;
    }
    // Like hits on creatures and carts: nothing during the attack cooldown, out of reach or through
    // walls; a driver cannot break the own boat.
    if (client.attackCooldown > 0.0 || boat->driver == client.id || !canReach(client, *boat)) {
        return true;
    }

    client.attackCooldown = QcServerCombat::playerAttackCooldown;
    client.swung = true;
    if (QcServerClient *driving = driver(*boat)) {
        leave(*driving);
    }
    releasePassenger(*boat);

    const std::optional<QcBoat> broken = m_state.entities().boats().remove(id);
    if (!broken) {
        return true;
    }
    m_sounds.play(breakSound, broken->motion.position);
    const std::optional<QcItemId> item = m_state.content().itemId(broken->item);
    if (item && !client.rules().infiniteItems) {
        m_state.entities().items().spawnScattered({*item, 1, 0}, broken->motion.position + QcVec3{0.0, 0.3, 0.0}, m_state.entities().newId());
    }
    return true;
}

bool QcServerBoats::steer(QcServerClient &client, const QcPlayerInput &input)
{
    QcBoat *boat = m_state.entities().boats().drivenBy(client.id);
    if (!boat) {
        return false;
    }
    // A spectator rides nothing (decision E-730): one who became one is no longer in the boat. One
    // moved away from his seat since it was set (a teleport, a knock-back) is no longer in it
    // either: the input moves him from where he is, and the boat does not fetch him back.
    if (!client.hasBody() || (client.motion.position - boat->driverSeat).length() > seatTolerance) {
        boat->driver = 0;
        return false;
    }

    client.yaw = input.yaw;
    client.pitch = input.pitch;
    client.sneaking = false;
    client.sprinting = false;
    if (input.sneak) {
        leave(client);
        return true;
    }

    // The same step the driver's client predicts with this input (QcPrediction).
    const QcBoatControls controls = QcBoatControls::of(input);
    boat->rowing = controls.forward != 0.0 || controls.turn != 0.0;
    QcBoatState state = boat->state();
    state.step(controls, m_state.world());
    boat->setState(state);
    seatRiders(*boat);
    return true;
}

void QcServerBoats::leave(QcServerClient &client)
{
    QcBoat *boat = m_state.entities().boats().drivenBy(client.id);
    if (!boat) {
        return;
    }

    boat->driver = 0;
    client.motion.position = exitSpot(m_state.world(), *boat);
    client.motion.velocity = {};
    client.motion.onGround = false;
    client.fallPeak = client.motion.position.y;
    client.stateDirty = true;
}

QcVec3 QcServerBoats::exitSpot(const QcBlockAccess &world, const QcBoat &boat)
{
    // Beside the boat first (to its right and left), then ahead and behind, then the corners; on a
    // bank one block above the water or level with the boat.
    const QcBlockPos cell = QcPhysics::blockContaining(boat.motion.position + QcVec3{0.0, 0.01, 0.0});
    const QcVec3 ahead = QcBoatBody::forward(boat.yaw);
    const QcBlockPos right = axisStep({-ahead.z, 0.0, ahead.x});
    const QcBlockPos front = axisStep(ahead);
    const QcBlockPos sides[] = {right, {-right.x, 0, -right.z}, front, {-front.x, 0, -front.z}, {1, 0, 1}, {1, 0, -1}, {-1, 0, 1}, {-1, 0, -1}};
    for (const QcBlockPos side : sides) {
        for (const int rise : {1, 0}) {
            const QcBlockPos candidate = cell.offset(side.x, rise, side.z);
            if (standsAt(world, candidate)) {
                return {candidate.x + 0.5, static_cast<double>(candidate.y), candidate.z + 0.5};
            }
        }
    }
    return boat.motion.position + QcVec3{0.0, QcBoatBody::height, 0.0};
}

QcServerClient *QcServerBoats::driver(const QcBoat &boat) const
{
    if (boat.driver == 0) {
        return nullptr;
    }
    for (const auto &client : m_state.clients()) {
        if (client->id == boat.driver) {
            return client.get();
        }
    }
    return nullptr;
}

void QcServerBoats::seatDriver(QcServerClient &client, QcBoat &boat) const
{
    boat.driverSeat = QcBoatBody::driverSeat(boat.motion.position, boat.yaw);
    client.motion.position = boat.driverSeat;
    client.motion.velocity = boat.motion.velocity;
    client.motion.onGround = true;
    client.motion.inWater = false;
    client.motion.flying = false;
    client.fallPeak = client.motion.position.y;
    client.stateDirty = true;
}

void QcServerBoats::seatRiders(QcBoat &boat) const
{
    if (QcServerClient *driving = driver(boat)) {
        seatDriver(*driving, boat);
    }
    QcCreature *creature = boat.passenger != 0 ? m_state.entities().creatures().find(boat.passenger) : nullptr;
    if (!creature) {
        return;
    }
    boat.passengerSeat = QcBoatBody::passengerSeat(boat.motion.position, boat.yaw);
    creature->motion.position = boat.passengerSeat;
    creature->motion.velocity = boat.motion.velocity;
    creature->motion.onGround = true;
    creature->motion.inWater = false;
    creature->yaw = boat.yaw;
    creature->pose = QcPose::Sitting;
    creature->vehicle = boat.id;
}

void QcServerBoats::releasePassenger(QcBoat &boat) const
{
    if (QcCreature *creature = boat.passenger != 0 ? m_state.entities().creatures().find(boat.passenger) : nullptr) {
        creature->vehicle = 0;
        creature->pose = QcPose::Standing;
    }
    boat.passenger = 0;
    boat.rest.reset();
}

void QcServerBoats::releaseLostRiders()
{
    for (QcBoat &boat : m_state.entities().boats().editableAll()) {
        const QcServerClient *driving = driver(boat);
        if (boat.driver != 0 && (!driving || !driving->hasBody() || (driving->motion.position - boat.driverSeat).length() > seatTolerance)) {
            boat.driver = 0;
        }
        const QcCreature *creature = boat.passenger != 0 ? m_state.entities().creatures().find(boat.passenger) : nullptr;
        if (boat.passenger != 0 && (!creature || (creature->motion.position - boat.passengerSeat).length() > seatTolerance)) {
            releasePassenger(boat);
        }
    }
}

void QcServerBoats::pickUpCreatures()
{
    QcBoatManager &boats = m_state.entities().boats();
    const bool seatFree = std::ranges::any_of(boats.all(), [](const QcBoat &boat) { return !boat.dormant && boat.passenger == 0; });
    if (!seatFree) {
        return;
    }

    // The creatures that may get in, by the columns their bodies cover: a boat looks only at those
    // in its own columns.
    QList<QcCreature> &creatures = m_state.entities().creatures().editableMobs();
    QList<QcAabb> boxes(creatures.size());
    QcBoxGrid grid;
    for (qsizetype index = 0; index < creatures.size(); ++index) {
        const QcCreature &creature = creatures.at(index);
        if (creature.dormant || creature.vehicle != 0 || creature.pose == QcPose::Lying || !fits(creature)) {
            continue;
        }
        if (const std::optional<QcAabb> box = m_state.entities().boxOf(creature)) {
            boxes[index] = *box;
            grid.add(index, *box);
        }
    }
    if (grid.isEmpty()) {
        return;
    }

    for (QcBoat &boat : boats.editableAll()) {
        if (boat.dormant || boat.passenger != 0) {
            continue;
        }
        const QcAabb reach = QcBoatManager::bodyOf(boat);
        const QcAabb touch{reach.min - QcVec3{pickupReach, 0.0, pickupReach}, reach.max + QcVec3{pickupReach, 0.0, pickupReach}};
        const QList<qsizetype> near = grid.near(touch);
        for (const qsizetype index : near) {
            QcCreature &creature = creatures[index];
            // One an earlier boat took in this tick stays there.
            if (creature.vehicle == 0 && boxes.at(index).intersects(touch)) {
                boat.passenger = creature.id;
                boat.rest.reset();
                creature.vehicle = boat.id;
                break;
            }
        }
    }
}

void QcServerBoats::releaseStrandedPassengers()
{
    // Creatures whose boat is gone (it left with its chunk, decision E-374, or was removed) or no
    // longer names them stand up and think again.
    QList<QcCreature> &creatures = m_state.entities().creatures().editableMobs();
    const bool anySeated = std::ranges::any_of(creatures, [](const QcCreature &creature) { return creature.vehicle != 0; });
    if (!anySeated) {
        return;
    }

    QHash<quint32, quint32> passengers;
    for (const QcBoat &boat : m_state.entities().boats().all()) {
        passengers.insert(boat.id, boat.passenger);
    }
    for (QcCreature &creature : creatures) {
        if (creature.vehicle != 0 && passengers.value(creature.vehicle) != creature.id) {
            creature.vehicle = 0;
            creature.pose = QcPose::Standing;
        }
    }
}

void QcServerBoats::tick()
{
    QcBoatManager &boats = m_state.entities().boats();
    releaseStrandedPassengers();
    if (boats.all().isEmpty()) {
        return;
    }

    // Boats in the void go, like everything that falls there (decision E-288).
    for (QcBoat &boat : boats.editableAll()) {
        if (QcWorld::isInVoid(boat.motion.position.y)) {
            if (QcServerClient *driving = driver(boat)) {
                driving->stateDirty = true;
            }
            releasePassenger(boat);
        }
    }
    boats.editableAll().removeIf([](const QcBoat &boat) { return QcWorld::isInVoid(boat.motion.position.y); });
    releaseLostRiders();

    // Living bodies that sit in no boat push the boats nobody drives.
    QList<QcAabb> bodies;
    for (const QcCreature &creature : m_state.entities().creatures().mobs()) {
        if (const std::optional<QcAabb> box = creature.dormant || creature.vehicle != 0 ? std::nullopt : m_state.entities().boxOf(creature)) {
            bodies.append(*box);
        }
    }
    for (const auto &client : m_state.clients()) {
        if (client->hasBody() && !boats.drivenBy(client->id) && !m_state.entities().minecarts().ridden(client->id)) {
            bodies.append(QcPhysics::boxAt(client->motion.position, QcPhysics::playerWidth, QcPhysics::playerHeight));
        }
    }

    boats.tick(m_state.world(), bodies);
    pickUpCreatures();
    for (QcBoat &boat : boats.editableAll()) {
        QcServerClient *driving = driver(boat);
        // Drivers hear of their own position from their inputs (steer); the tick changes nothing of it.
        const bool wasDirty = driving && driving->stateDirty;
        seatRiders(boat);
        if (driving) {
            driving->stateDirty = wasDirty;
        }
    }
}
