#include "server/projectiles/QcPlayerLaunchers.h"

#include "common/content/definitions/QcSoundKeys.h"
#include "common/physics/QcPhysics.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcServerInventory.h"
#include "server/gameplay/QcToolWear.h"
#include "server/projectiles/QcServerProjectiles.h"

#include <algorithm>
#include <optional>

namespace {

// The first inventory slot holding ammunition, and its projectile.
struct Ammunition
{
    int slot = -1;
    QString projectile;
};

// The first ammunition `launcher` takes from `inventory`; in creative mode without one the
// projectile of the first item the launcher takes (QcPlayerLaunchers::creativeProjectile).
std::optional<Ammunition> findAmmunition(const QcContentRegistry &content, const QcInventory &inventory, const QcLauncherDefinition &launcher,
                                         bool creative)
{
    if (const int slot = inventory.ammunitionSlot(content, launcher); slot >= 0) {
        return Ammunition{slot, content.item(inventory.slot(slot).item).projectile};
    }
    const QString projectile = creative ? QcPlayerLaunchers::creativeProjectile(content, launcher) : QString();
    if (projectile.isEmpty()) {
        return std::nullopt;
    }
    return Ammunition{-1, projectile};
}

} // namespace

QcPlayerLaunchers::QcPlayerLaunchers(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory,
                                     QcServerProjectiles &projectiles)
    : m_state(state)
    , m_sounds(sounds)
    , m_inventory(inventory)
    , m_projectiles(projectiles)
{
}

void QcPlayerLaunchers::draw(QcServerClient &client, QcMsg::DrawLauncher::Action action)
{
    const QcItemDefinition *held = m_state.heldItem(client);
    if (!held || !held->launcher || !mayDraw(client, held) || action == QcMsg::DrawLauncher::Action::Cancel) {
        client.drawing = false;
        return;
    }

    if (action == QcMsg::DrawLauncher::Action::Draw) {
        // Without ammunition only creative mode draws (it shoots the first projectile kind it takes).
        const bool armed = client.rules().infiniteItems || client.inventory.ammunitionSlot(m_state.content(), *held->launcher) >= 0;
        if (!client.drawing && armed) {
            client.drawing = true;
            client.drawStart = m_state.clock().tick();
            client.drawSlot = client.inventory.selectedHotbarSlot();
            m_sounds.play(QString::fromLatin1(QcSoundKeys::bowDraw), client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0});
        }
        return;
    }

    if (client.drawing) {
        client.drawing = false;
        const double seconds = static_cast<double>(m_state.clock().tick() - client.drawStart) * QcPhysics::tickDuration;
        shoot(client, held->launcher->powerAfter(seconds));
    }
}

void QcPlayerLaunchers::tick()
{
    for (const auto &client : m_state.clients()) {
        if (client->drawing && !mayDraw(*client, m_state.heldItem(*client))) {
            client->drawing = false;
        }
    }
}

bool QcPlayerLaunchers::mayDraw(const QcServerClient &client, const QcItemDefinition *held)
{
    // A draw belongs to the slot it began in: another bow there or in another slot starts anew.
    const bool sameSlot = !client.drawing || client.drawSlot == client.inventory.selectedHotbarSlot();
    return !client.dead && client.state == QcServerClient::State::Playing && held && held->launcher && sameSlot;
}

QString QcPlayerLaunchers::creativeProjectile(const QcContentRegistry &content, const QcLauncherDefinition &launcher)
{
    // By the kinds in their order, but the item's own kind: two kinds may share an item.
    for (const QcProjectileDefinition &kind : content.projectiles()) {
        const std::optional<QcItemId> item = content.itemId(kind.item);
        if (item && QcInventory::isAmmunition(content.item(*item), launcher)) {
            return content.item(*item).projectile;
        }
    }
    return {};
}

bool QcPlayerLaunchers::shoot(QcServerClient &client, double power)
{
    const QcContentRegistry &content = m_state.content();
    const QcItemDefinition *held = m_state.heldItem(client);
    if (client.dead || !held || !held->launcher || power < held->launcher->minPower || power <= 0.0) {
        return false;
    }

    const bool creative = client.rules().infiniteItems;
    const std::optional<Ammunition> ammunition = findAmmunition(content, client.inventory, *held->launcher, creative);
    if (!ammunition) {
        return false;
    }

    const QcVec3 eye = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const QcVec3 direction = QcProjectileFlight::direction(client.yaw, client.pitch);
    const QcProjectileManager::Launch shot{client.id,       ammunition->projectile,
                                           eye + direction * muzzle, direction * (held->launcher->speed * std::min(power, 1.0)),
                                           creative ? QcProjectilePickup::Remove : QcProjectilePickup::Item};
    if (!m_projectiles.launch(shot)) {
        return false;
    }
    m_sounds.play(QString::fromLatin1(QcSoundKeys::bowShoot), eye, 1.0f, static_cast<float>(0.8 + 0.4 * power));
    client.swung = true;
    if (creative) {
        return true;
    }

    if (ammunition->slot >= 0) {
        QcItemStack stack = client.inventory.slot(ammunition->slot);
        stack.count -= 1;
        client.inventory.setSlot(ammunition->slot, stack.count > 0 ? stack : QcItemStack{});
    }
    const int slot = client.inventory.selectedHotbarSlot();
    QcItemStack launcher = client.inventory.slot(slot);
    launcher = QcToolWear::worn(m_state, launcher);
    if (launcher.isEmpty()) {
        m_sounds.play(QString::fromLatin1(QcSoundKeys::itemBreak), client.motion.position);
    }
    client.inventory.setSlot(slot, launcher);
    m_inventory.send(client);
    return true;
}
