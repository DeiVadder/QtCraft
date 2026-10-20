#include "server/hanging/QcServerHanging.h"

#include "common/content/definitions/QcSoundKeys.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcChatText.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/gameplay/QcServerCombat.h"
#include "server/gameplay/QcServerInventory.h"
#include "server/hanging/QcHangingRules.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

const QString placeSound = u"qtcraft:block.wood.place"_s;
const QString turnSound = u"qtcraft:block.wood.hit"_s;
const QString breakSound = u"qtcraft:block.wood.break"_s;

} // namespace

QcServerHanging::QcServerHanging(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory)
    : m_state(state)
    , m_sounds(sounds)
    , m_inventory(inventory)
{
}

bool QcServerHanging::place(QcServerClient &client, const QcMsg::UseItem &message)
{
    const QcItemDefinition *held = m_state.heldItem(client);
    if (!held || !held->hangs) {
        return false;
    }

    QcHanging placed;
    placed.type = held->hangs->type;
    placed.face = message.face;
    placed.cell = qcNeighbour(message.target, message.face);
    std::optional<QcHanging> chosen;
    if (client.dead || client.rules().buildByAllowList || !QcLineOfSight::isWithinReach(client, placed.cell)) {
        chosen.reset();
    } else if (placed.type == QcHangingType::Painting) {
        chosen = QcHangingRules::painting(m_state.world(), m_state.entities().hangings(), *held->hangs, placed, m_state.random());
    } else if (QcHangingRules::fits(m_state.world(), m_state.entities().hangings(), placed)) {
        chosen = placed;
    }
    if (!chosen) {
        m_inventory.send(client);
        return true;
    }

    QcHangingEntity hanging;
    hanging.hanging = *chosen;
    hanging.item = held->name;
    const QcHangingEntity *spawned = spawn(m_state, hanging);
    if (!spawned) {
        client.connection->send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "Too many item frames and paintings in the world (at most %1)"),
                                                    {QString::number(m_state.entities().hangings().limit())}));
        m_inventory.send(client);
        return true;
    }

    if (!client.rules().infiniteItems) {
        client.inventory.removeFromSlot(client.inventory.selectedHotbarSlot(), 1);
    }
    m_sounds.play(placeSound, spawned->motion.position);
    m_inventory.send(client);
    return true;
}

QcHangingEntity *QcServerHanging::spawn(QcServerState &state, QcHangingEntity hanging)
{
    hanging.id = state.entities().newId();
    hanging.motion.position = hanging.hanging.centre();
    if (!state.entities().hangings().add(hanging)) {
        return nullptr;
    }
    state.noteContents(hanging.hanging.cell);
    return state.entities().hangings().find(hanging.id);
}

bool QcServerHanging::canReach(const QcServerClient &client, const QcHangingEntity &hanging) const
{
    // Measured to the nearest point of its box, so a large painting is in reach at its edge.
    const QcAabb body = hanging.hanging.box();
    const QcVec3 eye = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const QcVec3 nearest{std::clamp(eye.x, body.min.x, body.max.x), std::clamp(eye.y, body.min.y, body.max.y), std::clamp(eye.z, body.min.z, body.max.z)};
    return !client.dead && (nearest - eye).length() <= QcServerCombat::attackReach + 1.0 && QcLineOfSight::canSee(m_state.world(), client, body);
}

bool QcServerHanging::use(QcServerClient &client, quint32 id)
{
    const QcHangingEntity *hanging = m_state.entities().hangings().find(id);
    if (!hanging) {
        return false;
    }
    // A painting does nothing on a right click.
    if (hanging->hanging.type == QcHangingType::ItemFrame && canReach(client, *hanging)) {
        useFrame(client, id);
    }
    return true;
}

void QcServerHanging::useFrame(QcServerClient &client, quint32 id)
{
    QcHangingEntity &frame = *m_state.entities().hangings().find(id);
    if (!frame.held.isEmpty()) {
        frame.hanging.rotation = quint8((frame.hanging.rotation + 1) % QcHanging::rotations);
        m_sounds.play(turnSound, frame.motion.position);
        changed(frame);
        return;
    }

    const QcItemStack held = client.inventory.selectedStack();
    if (held.isEmpty()) {
        return;
    }
    frame.held = {held.item, 1, held.damage};
    frame.hanging.rotation = 0;
    if (!client.rules().infiniteItems) {
        client.inventory.removeFromSlot(client.inventory.selectedHotbarSlot(), 1);
    }
    m_sounds.play(QString::fromLatin1(QcSoundKeys::armourEquip), frame.motion.position);
    changed(frame);
    m_inventory.send(client);
}

bool QcServerHanging::hit(QcServerClient &client, quint32 id)
{
    QcHangingEntity *hanging = m_state.entities().hangings().find(id);
    if (!hanging) {
        return false;
    }
    // Like hits on stands: nothing during the attack cooldown, out of reach, through walls or in
    // adventure mode (decision E-735).
    if (client.attackCooldown > 0.0 || !canReach(client, *hanging) || client.rules().buildByAllowList) {
        return true;
    }

    client.attackCooldown = QcServerCombat::playerAttackCooldown;
    client.swung = true;
    if (!hanging->held.isEmpty()) {
        // The item comes out first; the frame stays.
        if (!client.rules().infiniteItems) {
            drop(hanging->held, *hanging);
        }
        hanging->held = {};
        hanging->hanging.rotation = 0;
        m_sounds.play(turnSound, hanging->motion.position);
        changed(*hanging);
        return true;
    }

    m_sounds.play(breakSound, hanging->motion.position);
    remove(id, !client.rules().infiniteItems);
    return true;
}

void QcServerHanging::remove(quint32 id, bool drops)
{
    const std::optional<QcHangingEntity> removed = m_state.entities().hangings().remove(id);
    if (!removed) {
        return;
    }

    m_state.noteContents(removed->hanging.cell);
    if (!drops) {
        return;
    }
    drop(removed->held, *removed);
    if (const std::optional<QcItemId> item = m_state.content().itemId(removed->item)) {
        drop({*item, 1, 0}, *removed);
    }
}

void QcServerHanging::tick()
{
    // Only those next to a changed block and a slice of the round (decision E-790), so the cost
    // stays flat with many frames and paintings.
    QcHangingManager &hangings = m_state.entities().hangings();
    QList<quint32> fallen;
    const QList<quint32> due = hangings.takeDueChecks();
    for (const quint32 id : due) {
        const QcHangingEntity *hanging = hangings.find(id);
        if (hanging && !hanging->dormant && !QcHangingRules::holds(m_state.world(), hanging->hanging).value_or(true) && !fallen.contains(id)) {
            fallen.append(id);
        }
    }
    for (const quint32 id : std::as_const(fallen)) {
        if (const QcHangingEntity *hanging = hangings.find(id)) {
            m_sounds.play(breakSound, hanging->motion.position);
        }
        remove(id, true);
    }
}

void QcServerHanging::drop(QcItemStack stack, const QcHangingEntity &hanging)
{
    if (!stack.isEmpty()) {
        m_state.entities().items().spawnScattered(stack, hanging.motion.position, m_state.entities().newId());
    }
}

void QcServerHanging::changed(const QcHangingEntity &hanging)
{
    m_state.entities().markStale(hanging.id);
    m_state.noteContents(hanging.hanging.cell);
}
