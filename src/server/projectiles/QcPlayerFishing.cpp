#include "server/projectiles/QcPlayerFishing.h"

#include "common/physics/QcPhysics.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcServerInventory.h"
#include "server/projectiles/QcPlayerLaunchers.h"

#include <QSet>

#include <algorithm>
#include <cmath>

namespace {

// How far a bite pulls the bobber under (m); it floats up again by itself.
constexpr double dipDepth = 0.3;
// Seconds the catch flies from the bobber to the player, and how far above the bobber it starts (m).
constexpr double catchFlightSeconds = 0.8;
constexpr double catchLift = 0.6;

int ticksOf(double seconds)
{
    return std::max(1, int(std::lround(seconds / QcPhysics::tickDuration)));
}

bool isPresent(const QcServerClient &client)
{
    return client.state == QcServerClient::State::Playing && !client.dead;
}

} // namespace

QcPlayerFishing::QcPlayerFishing(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory)
    : m_state(state)
    , m_sounds(sounds)
    , m_inventory(inventory)
{
}

int QcPlayerFishing::waitTicks(const QcFishingDefinition &rod, bool raining, QRandomGenerator &random)
{
    const double seconds = rod.minSeconds + random.generateDouble() * (rod.maxSeconds - rod.minSeconds);
    return ticksOf(seconds * (raining ? rod.rainFactor : 1.0));
}

const QcFishingLoot *QcPlayerFishing::draw(const QcFishingDefinition &rod, QRandomGenerator &random)
{
    int total = 0;
    for (const QcFishingLoot &entry : rod.loot) {
        total += entry.weight;
    }
    if (total <= 0) {
        return nullptr;
    }

    int roll = int(random.bounded(quint32(total)));
    for (const QcFishingLoot &entry : rod.loot) {
        roll -= entry.weight;
        if (roll < 0) {
            return &entry;
        }
    }
    return nullptr;
}

bool QcPlayerFishing::use(QcServerClient &client)
{
    const QcItemDefinition *held = m_state.heldItem(client);
    if (!isPresent(client) || !held || !held->fishing) {
        return false;
    }

    // Casting and reeling in share the cooldown of throws, so a held button does not flicker the bobber.
    const QcItemId rod = client.inventory.slot(client.inventory.selectedHotbarSlot()).item;
    const quint64 now = m_state.clock().tick();
    if (now < client.throwReady.value(rod, 0)) {
        return false;
    }

    const bool done = bobberOf(client) ? (reel(client, *held->fishing), true) : cast(client, *held->fishing);
    if (done) {
        client.swung = true;
        client.throwReady.insert(rod, now + quint64(useCooldownTicks));
    }
    return done;
}

bool QcPlayerFishing::rainsAt(QcBlockPos pos) const
{
    return m_rainsAt && m_rainsAt(pos);
}

bool QcPlayerFishing::cast(QcServerClient &client, const QcFishingDefinition &rod)
{
    const QcVec3 eye = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const QcVec3 direction = QcProjectileFlight::direction(client.yaw, client.pitch);
    const QcProjectileManager::Launch shot{.shooter = client.id,
                                           .type = rod.bobber,
                                           .position = eye + direction * QcPlayerLaunchers::muzzle,
                                           .velocity = direction * rod.speed,
                                           .pickup = QcProjectilePickup::Never,
                                           .transient = true};
    const quint32 id = m_state.entities().newId();
    if (!m_state.entities().projectiles().launch(shot, id)) {
        return false;
    }

    client.fishing = {.bobber = id, .slot = client.inventory.selectedHotbarSlot()};
    if (!rod.castSound.isEmpty()) {
        m_sounds.play(rod.castSound, eye);
    }
    return true;
}

void QcPlayerFishing::reel(QcServerClient &client, const QcFishingDefinition &rod)
{
    const QcProjectile *bobber = bobberOf(client);
    const QcFishingLoot *loot = bobber && client.fishing.biteTicks > 0 ? draw(rod, m_state.random()) : nullptr;
    if (loot) {
        land(client, *bobber, *loot);
        m_inventory.wearHeld(client);
    }
    stop(client);
}

void QcPlayerFishing::land(const QcServerClient &client, const QcProjectile &bobber, const QcFishingLoot &loot)
{
    const std::optional<QcItemId> item = m_state.content().itemId(loot.item);
    if (!item) {
        return;
    }

    // Thrown from just above the water (in it, the water's drag would hold it back) so that it comes
    // down at the player's feet after catchFlightSeconds, against gravity; the drag of items makes it
    // fall a little short, close enough to be picked up.
    const QcVec3 from = bobber.motion.position + QcVec3{0.0, catchLift, 0.0};
    const QcVec3 to = client.motion.position + QcVec3{0.0, 0.5, 0.0};
    const double t = catchFlightSeconds;
    const QcVec3 velocity{(to.x - from.x) / t, (to.y - from.y) / t + QcPhysics::gravity * t * 0.5, (to.z - from.z) / t};
    QcItemEntityManager::Drop drop;
    drop.stack = {*item, loot.count, 0};
    drop.position = from;
    drop.velocity = velocity;
    drop.pickupDelay = 0.0;
    m_state.entities().items().spawn(drop, m_state.entities().newId());
}

void QcPlayerFishing::tick()
{
    for (const auto &client : m_state.clients()) {
        if (client->fishing.bobber == 0) {
            continue;
        }

        QcProjectile *bobber = bobberOf(*client);
        const QcFishingDefinition *rod = isPresent(*client) ? rodOf(*client) : nullptr;
        if (!bobber || !rod || !tickBobber(*client, *bobber, *rod)) {
            stop(*client);
        }
    }
    removeStrays();
}

void QcPlayerFishing::removeStrays()
{
    QList<QcProjectile> &all = m_state.entities().projectiles().editableAll();
    if (std::ranges::none_of(all, &QcProjectile::transient)) {
        return;
    }

    QSet<quint32> cast;
    for (const auto &client : m_state.clients()) {
        cast.insert(client->fishing.bobber);
    }
    all.removeIf([&cast](const QcProjectile &projectile) { return projectile.transient && !cast.contains(projectile.id); });
}

bool QcPlayerFishing::tickBobber(QcServerClient &client, QcProjectile &bobber, const QcFishingDefinition &rod)
{
    if ((bobber.motion.position - client.motion.position).length() > rod.maxDistance) {
        return false;
    }

    // Out of the water (still flying, on land) nothing bites; the wait starts once it floats.
    QcServerClient::Fishing &fishing = client.fishing;
    if (!bobber.motion.inWater) {
        fishing.waitTicks = -1;
        fishing.biteTicks = 0;
        return true;
    }

    if (fishing.biteTicks > 0) {
        // The fish got away: the next one comes after a new wait.
        if (--fishing.biteTicks == 0) {
            fishing.waitTicks = -1;
        }
        return true;
    }
    if (fishing.waitTicks < 0) {
        const QcBlockPos at = QcPhysics::blockContaining(bobber.motion.position);
        fishing.waitTicks = waitTicks(rod, rainsAt(at), m_state.random());
    }
    if (--fishing.waitTicks <= 0) {
        bite(client, bobber, rod);
    }
    return true;
}

void QcPlayerFishing::bite(QcServerClient &client, QcProjectile &bobber, const QcFishingDefinition &rod)
{
    client.fishing.waitTicks = 0;
    client.fishing.biteTicks = ticksOf(rod.biteSeconds);
    bobber.motion.position.y -= dipDepth;
    bobber.motion.velocity = {};
    if (!rod.biteSound.isEmpty()) {
        m_sounds.play(rod.biteSound, bobber.motion.position);
    }
}

const QcFishingDefinition *QcPlayerFishing::rodOf(const QcServerClient &client) const
{
    const QcItemDefinition *held = m_state.heldItem(client);
    const bool sameSlot = client.inventory.selectedHotbarSlot() == client.fishing.slot;
    return sameSlot && held && held->fishing ? &*held->fishing : nullptr;
}

QcProjectile *QcPlayerFishing::bobberOf(const QcServerClient &client)
{
    if (client.fishing.bobber == 0) {
        return nullptr;
    }
    QList<QcProjectile> &all = m_state.entities().projectiles().editableAll();
    const auto it = std::ranges::find(all, client.fishing.bobber, &QcProjectile::id);
    return it == all.end() ? nullptr : &*it;
}

void QcPlayerFishing::stop(QcServerClient &client)
{
    const quint32 id = client.fishing.bobber;
    client.fishing = {};
    if (id != 0) {
        m_state.entities().projectiles().editableAll().removeIf([id](const QcProjectile &projectile) { return projectile.id == id; });
    }
}
