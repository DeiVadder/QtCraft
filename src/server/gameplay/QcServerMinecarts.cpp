#include "QcServerMinecarts.h"

#include "QcBlockScreens.h"
#include "QcChatText.h"
#include "QcLineOfSight.h"
#include "QcMinecartBody.h"
#include "QcMinecartMotion.h"
#include "QcPhysics.h"
#include "QcServerCombat.h"
#include "QcServerInventory.h"
#include "QcServerSounds.h"
#include "QcServerState.h"
#include "QcWorldConstants.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

const QString placeSound = u"qtcraft:block.stone.place"_s;
const QString breakSound = u"qtcraft:block.stone.break"_s;

bool isPresent(const QcServerClient &client)
{
    return client.state == QcServerClient::State::Playing && !client.dead;
}

// Whether a player standing with the feet at the bottom of `cell` fits there.
bool fitsPlayer(const QcBlockAccess &world, QcBlockPos cell)
{
    for (const QcBlockPos pos : {cell, cell.offset(0, 1, 0)}) {
        if (!world.blockAt(pos) || !QcPhysics::collisionBoxesAt(world, pos).isEmpty() || QcPhysics::isLiquidAt(world, pos)) {
            return false;
        }
    }
    return true;
}

} // namespace

QcServerMinecarts::QcServerMinecarts(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory, QcBlockScreens &screens)
    : m_state(state)
    , m_sounds(sounds)
    , m_inventory(inventory)
    , m_screens(screens)
    , m_hoppers(state, screens)
{
}

std::optional<QcVec3> QcServerMinecarts::restingPlace(QcBlockPos cell) const
{
    const std::optional<QcBlockId> block = m_state.world().blockAt(cell);
    const QcMinecartManager &carts = m_state.entities().minecarts();
    const std::optional<QcRails::Track> &track = block ? carts.rails().trackOf(*block) : std::nullopt;
    if (!track) {
        return std::nullopt;
    }
    return (QcRails::endPoint(cell, track->ends[0]) + QcRails::endPoint(cell, track->ends[1])) * 0.5;
}

quint32 QcServerMinecarts::vehicleOf(const QcServerClient &client) const
{
    const QcMinecart *cart = m_state.entities().minecarts().ridden(client.id);
    return cart ? cart->id : 0;
}

QcMinecart *QcServerMinecarts::spawn(QcMinecart cart)
{
    cart.id = m_state.entities().newId();
    cart.rider = 0;
    cart.yaw = qcNormalizedAngle(cart.yaw);
    if (!m_state.entities().minecarts().add(cart)) {
        return nullptr;
    }
    return m_state.entities().minecarts().find(cart.id);
}

bool QcServerMinecarts::place(QcServerClient &client, const QcMsg::UseItem &message)
{
    const QcItemDefinition *held = m_state.heldItem(client);
    if (!held || !held->placesCart) {
        return false;
    }

    const std::optional<QcVec3> spot = restingPlace(message.target);
    const auto occupied = [this, &spot] {
        const QcAabb box = QcPhysics::boxAt(*spot, QcMinecartBody::width, QcMinecartBody::height);
        return std::ranges::any_of(m_state.entities().minecarts().all(),
                                   [&box](const QcMinecart &other) { return QcMinecartManager::bodyOf(other).intersects(box); });
    };
    if (client.dead || !spot || !QcLineOfSight::isWithinReach(client, message.target) || occupied()) {
        m_inventory.send(client);
        return true;
    }

    QcMinecart cart = m_state.entities().minecarts().fromItem(held->name);
    cart.motion.position = *spot;
    cart.motion.onGround = true;
    const std::optional<QcMinecartMotion::Segment> segment = QcMinecartMotion::segmentAt(*spot, m_state.entities().minecarts().rails(), m_state.world());
    cart.yaw = segment ? QcMinecartMotion::yawAlong(segment->direction) : 0.0f;
    if (!spawn(cart)) {
        client.connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "Too many minecarts in the world (at most %1)"),
                                                    {QString::number(m_state.entities().minecarts().limit())}));
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

bool QcServerMinecarts::canReach(const QcServerClient &client, const QcMinecart &cart) const
{
    const QcAabb body = QcMinecartManager::bodyOf(cart);
    const QcVec3 eye = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const QcVec3 centre = (body.min + body.max) * 0.5;
    return !client.dead && (centre - eye).length() <= QcServerCombat::attackReach + 1.0 && QcLineOfSight::canSee(m_state.world(), client, body);
}

bool QcServerMinecarts::use(QcServerClient &client, quint32 id)
{
    QcMinecart *cart = m_state.entities().minecarts().find(id);
    if (!cart) {
        return false;
    }
    if (vehicleOf(client) != 0 || !canReach(client, *cart)) {
        return true;
    }
    if (!cart->stacks.isEmpty()) {
        m_screens.openCart(client, *cart);
        return true;
    }
    if (cart->rider != 0) {
        return true;
    }

    cart->rider = client.id;
    // A rider changes how the cart rolls (and an activator rail puts him out): the cart wakes.
    cart->rest.reset();
    client.mining.pos.reset();
    seat(client, *cart);
    return true;
}

bool QcServerMinecarts::hit(QcServerClient &client, quint32 id)
{
    const QcMinecart *cart = m_state.entities().minecarts().find(id);
    if (!cart) {
        return false;
    }
    // Like hits on creatures: nothing during the attack cooldown, out of reach or through walls; a
    // rider cannot break the own cart.
    if (client.attackCooldown > 0.0 || cart->rider == client.id || !canReach(client, *cart)) {
        return true;
    }

    client.attackCooldown = QcServerCombat::playerAttackCooldown;
    client.swung = true;
    if (QcServerClient *riding = rider(*cart)) {
        leave(*riding);
    }

    const std::optional<QcMinecart> broken = m_state.entities().minecarts().remove(id);
    if (!broken) {
        return true;
    }
    m_sounds.play(breakSound, broken->motion.position);
    const QcVec3 drop = broken->motion.position + QcVec3{0.0, 0.3, 0.0};
    const std::optional<QcItemId> item = m_state.content().itemId(broken->item);
    if (item && !client.rules().infiniteItems) {
        m_state.entities().items().spawnScattered({*item, 1, 0}, drop, m_state.entities().newId());
    }
    // A container cart spills what it carried, also in creative mode (like a broken chest).
    for (const QcItemStack &stack : broken->stacks) {
        if (!stack.isEmpty()) {
            m_state.entities().items().spawnScattered(stack, drop, m_state.entities().newId());
        }
    }
    return true;
}

bool QcServerMinecarts::steer(QcServerClient &client, const QcPlayerInput &input)
{
    QcMinecart *cart = m_state.entities().minecarts().ridden(client.id);
    if (!cart) {
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

    if (input.forward > 0.0f) {
        push(*cart, double(input.yaw), double(input.forward));
    }
    return true;
}

void QcServerMinecarts::push(QcMinecart &cart, double yaw, double forward) const
{
    // Only on rails: off them the forward key does nothing (no steering through the air).
    const std::optional<QcMinecartMotion::Segment> segment =
        QcMinecartMotion::segmentAt(cart.motion.position, m_state.entities().minecarts().rails(), m_state.world());
    if (!segment) {
        return;
    }
    const QcVec3 level{segment->direction.x, 0.0, segment->direction.z};
    if (level.length() < 1e-9) {
        return;
    }

    // Along the track, as far as the view runs along it, and only up to the top speed along the
    // track in the way the rider looks.
    const double radians = qDegreesToRadians(yaw);
    const QcVec3 view{std::sin(radians), 0.0, -std::cos(radians)};
    const double share = view.dot(level.normalized());
    const QcVec3 ahead = segment->direction * (share >= 0.0 ? 1.0 : -1.0);
    const double speed = cart.motion.velocity.dot(ahead);
    if (speed >= riderPushTopSpeed) {
        return;
    }
    const double added = riderPushAcceleration * QcPhysics::tickDuration * forward * std::abs(share);
    cart.motion.velocity = cart.motion.velocity + ahead * (std::min(speed + added, riderPushTopSpeed) - speed);
}

void QcServerMinecarts::leave(QcServerClient &client)
{
    QcMinecart *cart = m_state.entities().minecarts().ridden(client.id);
    if (!cart) {
        return;
    }

    cart->rider = 0;
    client.motion.position = exitSpot(m_state.world(), *cart);
    client.motion.velocity = {};
    client.motion.onGround = false;
    client.fallPeak = client.motion.position.y;
    client.stateDirty = true;
}

QcVec3 QcServerMinecarts::exitSpot(const QcBlockAccess &world, const QcMinecart &cart)
{
    const QcBlockPos cell = QcPhysics::blockContaining(cart.motion.position + QcVec3{0.0, 0.01, 0.0});
    const QcBlockPos candidates[] = {cell, cell.offset(1, 0, 0), cell.offset(-1, 0, 0), cell.offset(0, 0, 1), cell.offset(0, 0, -1)};
    for (const QcBlockPos candidate : candidates) {
        if (fitsPlayer(world, candidate)) {
            return {candidate.x + 0.5, double(candidate.y), candidate.z + 0.5};
        }
    }
    return cart.motion.position;
}

QcServerClient *QcServerMinecarts::rider(const QcMinecart &cart) const
{
    if (cart.rider == 0) {
        return nullptr;
    }
    for (const auto &client : m_state.clients()) {
        if (client->id == cart.rider) {
            return client.get();
        }
    }
    return nullptr;
}

void QcServerMinecarts::seat(QcServerClient &client, QcMinecart &cart) const
{
    cart.seat = cart.motion.position + QcVec3{0.0, QcMinecartBody::seatOffset, 0.0};
    client.motion.position = cart.seat;
    client.motion.velocity = cart.motion.velocity;
    client.motion.onGround = true;
    client.motion.inWater = false;
    client.motion.flying = false;
    client.fallPeak = client.motion.position.y;
    client.stateDirty = true;
}

void QcServerMinecarts::releaseLostRiders()
{
    for (QcMinecart &cart : m_state.entities().minecarts().editableAll()) {
        if (cart.rider == 0) {
            continue;
        }
        const QcServerClient *riding = rider(cart);
        if (!riding || !isPresent(*riding) || (riding->motion.position - cart.seat).length() > seatTolerance) {
            cart.rider = 0;
        }
    }
}

void QcServerMinecarts::tick()
{
    QcMinecartManager &carts = m_state.entities().minecarts();
    // Carts in the void go, like everything that falls there (decision E-288).
    carts.editableAll().removeIf([this](const QcMinecart &cart) {
        if (!QcWorld::isInVoid(cart.motion.position.y)) {
            return false;
        }
        if (QcServerClient *riding = rider(cart)) {
            riding->stateDirty = true;
        }
        return true;
    });
    releaseLostRiders();

    // Living bodies that ride nothing push the carts they walk into.
    QList<QcAabb> bodies;
    for (const QcCreature &creature : m_state.entities().creatures().mobs()) {
        if (const std::optional<QcAabb> box = creature.dormant ? std::nullopt : m_state.entities().boxOf(creature)) {
            bodies.append(*box);
        }
    }
    for (const auto &client : m_state.clients()) {
        if (isPresent(*client) && !carts.ridden(client->id)) {
            bodies.append(QcPhysics::boxAt(client->motion.position, QcPhysics::playerWidth, QcPhysics::playerHeight));
        }
    }

    carts.tick(m_state.world(), bodies, QcPhysics::tickDuration);
    // A powered activator rail puts the rider out.
    for (const quint32 id : carts.activated()) {
        const QcMinecart *cart = carts.find(id);
        if (QcServerClient *riding = cart ? rider(*cart) : nullptr) {
            leave(*riding);
        }
    }
    for (QcMinecart &cart : carts.editableAll()) {
        if (QcServerClient *riding = rider(cart)) {
            const bool wasDirty = riding->stateDirty;
            seat(*riding, cart);
            // Riders hear of their own position only when something else changed: the client shows
            // them in the cart anyway.
            riding->stateDirty = wasDirty;
        }
    }
    m_hoppers.tick();
}
