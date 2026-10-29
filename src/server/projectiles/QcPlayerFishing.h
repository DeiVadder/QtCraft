// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcProjectileDefinition.h"
#include "common/core/QcBlockPos.h"

#include <QRandomGenerator>

#include <functional>

class QcServerInventory;
class QcServerSounds;
class QcServerState;
struct QcProjectile;
struct QcServerClient;

// Players fishing with a rod (decision E-503; QcItemDefinition::fishing, QcMsg::ThrowItem): the
// first use casts the rod's bobber, a projectile that floats on water (QcProjectileDefinition::floats);
// the second reels it in. While the bobber floats on water, a fish bites after a random wait
// between the rod's minSeconds and maxSeconds (times rainFactor where it rains on the bobber); the
// bite lasts biteSeconds, the bobber dips and splashes. Reeling in during the bite throws one entry
// of the rod's loot table, drawn by weight, from the bobber to the player and wears the rod by one;
// reeling in at any other time catches nothing. The bobber goes when the player reels in, dies,
// leaves, selects another hotbar slot or item, or is more than the rod's maxDistance from it; it is
// never saved (QcProjectile::transient), so a chunk that unloads takes it along.
class QcPlayerFishing
{
public:
    // Ticks after a cast or a reel before the rod does anything again (as the throw cooldown).
    static constexpr int useCooldownTicks = 5;

    QcPlayerFishing(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory);

    // Where it rains, for the shorter wait; without it the wait never shortens.
    void setRainCheck(std::function<bool(QcBlockPos)> rainsAt) { m_rainsAt = std::move(rainsAt); }
    // Whether it rains on a bobber in the block `pos`.
    bool rainsAt(QcBlockPos pos) const;

    // The use button with the selected item (QcMsg::ThrowItem): casts when the player has no bobber
    // out, else reels in; the arm swings only then. False when the held item does not fish, within
    // useCooldownTicks of the last cast or reel, or when the cast failed.
    bool use(QcServerClient &client);
    // One tick of every player's bobber: the wait, the bite, and the bobbers that go, also those
    // nobody fishes with any more (their player left).
    void tick();

    // The ticks of a wait drawn from `random` for `rod`, in rain when `raining` (at least one tick).
    static int waitTicks(const QcFishingDefinition &rod, bool raining, QRandomGenerator &random);
    // The loot entry `random` draws by weight; nullptr for an empty table.
    static const QcFishingLoot *draw(const QcFishingDefinition &rod, QRandomGenerator &random);

private:
    bool cast(QcServerClient &client, const QcFishingDefinition &rod);
    void reel(QcServerClient &client, const QcFishingDefinition &rod);
    // One player's bobber; false when it goes.
    bool tickBobber(QcServerClient &client, QcProjectile &bobber, const QcFishingDefinition &rod);
    void bite(QcServerClient &client, QcProjectile &bobber, const QcFishingDefinition &rod);
    // Throws the catch from the bobber towards the player.
    void land(const QcServerClient &client, const QcProjectile &bobber, const QcFishingLoot &loot);
    // The rod in the player's hand, while it is still the slot the bobber was cast from.
    const QcFishingDefinition *rodOf(const QcServerClient &client) const;
    QcProjectile *bobberOf(const QcServerClient &client);
    // Removes the player's bobber and forgets it.
    void stop(QcServerClient &client);
    // Removes the transient projectiles no player fishes with.
    void removeStrays();

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
    std::function<bool(QcBlockPos)> m_rainsAt;
};
