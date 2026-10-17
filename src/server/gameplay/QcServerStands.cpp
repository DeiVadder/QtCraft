#include "server/gameplay/QcServerStands.h"

#include "common/content/definitions/QcSoundKeys.h"
#include "common/gameplay/QcArmour.h"
#include "common/physics/QcPhysics.h"
#include "common/physics/QcRaycast.h"
#include "common/protocol/QcChatText.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/gameplay/QcServerCombat.h"
#include "server/gameplay/QcServerInventory.h"

#include <QtMath>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

const QString placeSound = u"qtcraft:block.wood.place"_s;
const QString hitSound = u"qtcraft:block.wood.hit"_s;
const QString breakSound = u"qtcraft:block.wood.break"_s;

QcVec3 viewDirection(const QcServerClient &client)
{
    const double yaw = qDegreesToRadians(double(client.yaw));
    const double pitch = qDegreesToRadians(double(client.pitch));
    return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
}

} // namespace

QcServerStands::QcServerStands(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory)
    : m_state(state)
    , m_sounds(sounds)
    , m_inventory(inventory)
{
}

float QcServerStands::facingYaw(float playerYaw)
{
    const float facing = playerYaw + 180.0f;
    return qcNormalizedAngle(std::round(facing / yawStep) * yawStep);
}

QcEquipmentSlot QcServerStands::slotAtHeight(double height)
{
    // Heights of the humanoid model: boots up to the shins, leggings to the hips, then the chest.
    if (height < 0.35) {
        return QcEquipmentSlot::Feet;
    }
    if (height < 0.75) {
        return QcEquipmentSlot::Legs;
    }
    if (height < 1.4) {
        return QcEquipmentSlot::Chest;
    }
    return QcEquipmentSlot::Head;
}

bool QcServerStands::isFreeForStand(QcBlockPos feet) const
{
    for (const QcBlockPos pos : {feet, feet.offset(0, 1, 0)}) {
        const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
        if (!block || !m_state.content().isValidBlock(*block) || !m_state.content().state(*block).collision.isEmpty()
            || m_state.content().block(*block).liquid) {
            return false;
        }
    }

    const QcAabb cell{{double(feet.x), double(feet.y), double(feet.z)}, {feet.x + 1.0, feet.y + 2.0, feet.z + 1.0}};
    return std::ranges::none_of(m_state.entities().stands().stands(), [&](const auto &stand) { return QcArmourStandManager::bodyOf(stand).intersects(cell); });
}

bool QcServerStands::place(QcServerClient &client, const QcMsg::UseItem &message)
{
    const QcItemDefinition *held = m_state.heldItem(client);
    if (!held || !held->placesStand) {
        return false;
    }

    const QcBlockPos feet = message.target.offset(0, 1, 0);
    const std::optional<QcBlockId> ground = m_state.world().blockAt(message.target);
    const bool onTop = message.face == QcBlockFace::Up && ground && m_state.content().isValidBlock(*ground)
                       && m_state.content().block(*ground).solid;
    if (client.dead || !onTop || !QcLineOfSight::isWithinReach(client, feet) || !isFreeForStand(feet)) {
        m_inventory.send(client);
        return true;
    }

    QcArmourStand stand;
    stand.motion.position = {feet.x + 0.5, double(feet.y), feet.z + 0.5};
    stand.yaw = facingYaw(client.yaw);
    stand.item = held->name;
    if (!spawn(stand)) {
        client.connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "Too many armour stands in the world (at most %1)"),
                                                    {QString::number(m_state.entities().stands().limit())}));
        m_inventory.send(client);
        return true;
    }

    if (!client.rules().infiniteItems) {
        client.inventory.removeFromSlot(client.inventory.selectedHotbarSlot(), 1);
    }
    m_sounds.play(placeSound, stand.motion.position);
    m_inventory.send(client);
    return true;
}

QcArmourStand *QcServerStands::spawn(QcArmourStand stand)
{
    stand.id = m_state.entities().newId();
    stand.pose = stand.pose.normalized();
    stand.yaw = qcNormalizedAngle(stand.yaw);
    if (!m_state.entities().stands().add(stand)) {
        return nullptr;
    }
    return m_state.entities().stands().find(stand.id);
}

void QcServerStands::dropReplaced(const QcArmourStand &before, const QcArmourStand &after)
{
    QList<QcItemStack> replaced;
    for (std::size_t slot = 0; slot < QcEquipment::slotCount; ++slot) {
        if (before.equipment.stacks[slot] != after.equipment.stacks[slot]) {
            replaced.append(before.equipment.stacks[slot]);
        }
    }
    if (before.held != after.held) {
        replaced.append(before.held);
    }

    const QcVec3 position = before.motion.position + QcVec3{0.0, 0.5, 0.0};
    for (const QcItemStack &stack : std::as_const(replaced)) {
        if (!stack.isEmpty()) {
            m_state.entities().items().spawnScattered(stack, position, m_state.entities().newId());
        }
    }
}

void QcServerStands::changed(const QcArmourStand &stand)
{
    m_state.entities().markStale(stand.id);
}

bool QcServerStands::canReach(const QcServerClient &client, const QcArmourStand &stand) const
{
    const QcAabb body = QcArmourStandManager::bodyOf(stand);
    const QcVec3 eye = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const QcVec3 centre = (body.min + body.max) * 0.5;
    return !client.dead && (centre - eye).length() <= QcServerCombat::attackReach + 1.0 && QcLineOfSight::canSee(m_state.world(), client, body);
}

std::optional<double> QcServerStands::aimedHeight(const QcServerClient &client, const QcArmourStand &stand) const
{
    const QcVec3 eye = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const QcVec3 direction = viewDirection(client);
    const std::optional<double> distance =
        QcRaycast::intersectBox(eye, direction, QcArmourStandManager::bodyOf(stand), QcServerCombat::attackReach + 2.0);
    if (!distance) {
        return std::nullopt;
    }

    const double scale = stand.pose.small ? 0.5 : 1.0;
    return ((eye + direction * *distance).y - stand.motion.position.y) / scale;
}

bool QcServerStands::use(QcServerClient &client, quint32 id)
{
    QcArmourStand *stand = m_state.entities().stands().find(id);
    if (!stand) {
        return false;
    }
    if (!canReach(client, *stand)) {
        return true;
    }

    const QcItemStack held = client.inventory.selectedStack();
    const QcArmourDefinition *armour = held.isEmpty() ? nullptr : QcArmour::of(m_state.content(), held.item);
    if (armour) {
        swapWithHand(client, *stand, armour->slot);
    } else if (!held.isEmpty()) {
        // Any other item goes into the stand's hand, when it shows arms.
        if (stand->pose.arms) {
            swapWithHand(client, *stand, std::nullopt);
        }
    } else {
        // An empty hand takes what is worn where the player points, else what the stand holds.
        const QcEquipmentSlot slot = slotAtHeight(aimedHeight(client, *stand).value_or(1.0));
        swapWithHand(client, *stand, stand->equipment.at(slot).isEmpty() && !stand->held.isEmpty() ? std::nullopt : std::optional(slot));
    }
    return true;
}

void QcServerStands::swapWithHand(QcServerClient &client, QcArmourStand &stand, std::optional<QcEquipmentSlot> slot)
{
    const int hand = client.inventory.selectedHotbarSlot();
    const QcItemStack held = client.inventory.slot(hand);
    QcItemStack &onStand = slot ? stand.equipment.at(*slot) : stand.held;
    if (held.isEmpty() && onStand.isEmpty()) {
        return;
    }

    // One item goes onto the stand; the rest of a larger stack stays in the hand and what the stand
    // had goes into the inventory (nothing happens without room for it).
    QcItemStack given = held;
    if (!given.isEmpty()) {
        given.count = 1;
    }
    QcInventory inventory = client.inventory;
    if (held.count > 1) {
        inventory.removeFromSlot(hand, 1);
        if (!onStand.isEmpty() && inventory.add(onStand.item, onStand.count, m_state.content(), onStand.damage) > 0) {
            return;
        }
    } else {
        inventory.setSlot(hand, onStand);
    }

    client.inventory = inventory;
    onStand = given;
    m_sounds.play(QString::fromLatin1(QcSoundKeys::armourEquip), stand.motion.position + QcVec3{0.0, 1.0, 0.0});
    changed(stand);
    m_inventory.send(client);
}

bool QcServerStands::hit(QcServerClient &client, quint32 id)
{
    QcArmourStand *stand = m_state.entities().stands().find(id);
    if (!stand) {
        return false;
    }
    // Like hits on creatures: nothing during the attack cooldown, out of reach or through walls.
    if (client.attackCooldown > 0.0 || !canReach(client, *stand)) {
        return true;
    }

    client.attackCooldown = QcServerCombat::playerAttackCooldown;
    client.swung = true;
    if (stand->hitBy == client.id && stand->sinceHit < QcArmourStand::breakWindow) {
        m_sounds.play(breakSound, stand->motion.position);
        breakStand(id, true);
        return true;
    }

    stand->sinceHit = 0.0;
    stand->hitBy = client.id;
    m_sounds.play(hitSound, stand->motion.position);
    return true;
}

void QcServerStands::breakStand(quint32 id, bool drops)
{
    const std::optional<QcArmourStand> stand = m_state.entities().stands().remove(id);
    if (!stand || !drops) {
        return;
    }

    const QcVec3 position = stand->motion.position + QcVec3{0.0, 0.5, 0.0};
    QList<QcItemStack> stacks(stand->equipment.stacks.begin(), stand->equipment.stacks.end());
    stacks.append(stand->held);
    if (const std::optional<QcItemId> item = m_state.content().itemId(stand->item)) {
        stacks.append({*item, 1, 0});
    }

    for (const QcItemStack &stack : std::as_const(stacks)) {
        if (!stack.isEmpty()) {
            m_state.entities().items().spawnScattered(stack, position, m_state.entities().newId());
        }
    }
}

void QcServerStands::tick()
{
    m_state.entities().stands().tick(QcPhysics::tickDuration);
}
