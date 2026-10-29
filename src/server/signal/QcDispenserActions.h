// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcBlockDefinition.h"
#include "common/core/QcBlockFace.h"
#include "common/gameplay/QcEquipment.h"
#include "common/gameplay/QcItemStack.h"
#include "server/machines/QcInventoryAccess.h"

#include <QList>

#include <functional>

class QcServerBlocks;
class QcServerSounds;
class QcSignalTable;
struct QcServerClient;
class QcServerState;

// What Signal Dispensers and droppers do when their delay ends (decision E-345). Each picks one of
// its non-empty slots at random, from a generator seeded with the world's seed, the tick and its
// contents (so the same world plays the same, wherever and however the dispenser stands). A dropper
// facing a container puts the item into it like a hopper (all droppers of a step together,
// QcInventoryAccess::resolve), otherwise it throws it. A dispenser uses the item by its `dispense`
// action from the data (pour or take a liquid, put armour on a player or armour stand in front, light
// fire in front with flint and steel, shoot ammunition as its projectile, decision E-621) and
// throws it out when the action fails (flint and steel stays) or the item has none, also in front of a container. Seeds are thrown, never planted: they plant themselves
// on farmland (decision E-294). Actions of one step that need the same cell all fail (no order
// decides).
class QcDispenserActions
{
public:
    // Speed of a thrown item along the facing and, sideways, upwards (m/s).
    static constexpr double throwSpeed = 4.0;
    static constexpr double throwLift = 2.0;
    // Speed (m/s) of a shot projectile (the reference's 1.1 blocks a tick) and how far in front of
    // the dispenser's middle it starts (just outside the front face).
    static constexpr double shotSpeed = 22.0;
    static constexpr double shotOffset = 0.7;

    using EquipPlayer = std::function<void(QcServerClient &, const QcEquipment &)>;
    // Lights fire at a position like flint and steel (QcFireIgnition::strike); false when nothing caught.
    using Ignite = std::function<bool(QcBlockPos)>;

    QcDispenserActions(QcServerState &state, QcServerBlocks &blocks, QcServerSounds &sounds, const QcSignalTable &table);

    // Called with the player and the new equipment when a dispenser puts armour on a player.
    void setEquipPlayer(EquipPlayer equip) { m_equipPlayer = std::move(equip); }
    void setIgnite(Ignite ignite) { m_ignite = std::move(ignite); }
    // Fires the dispensers at `positions` together; returns the work done.
    qsizetype fire(const QList<QcBlockPos> &positions);

private:
    struct Shot
    {
        QcBlockPos dispenser;
        QcBlockFace facing = QcBlockFace::North;
        int slot = -1;
        QcItemStack unit;
    };

    // The shot of one dispenser, or nothing when it is empty or no dispenser.
    [[nodiscard]] std::optional<Shot> aim(QcBlockPos pos) const;
    // Droppers put their items into containers in front; returns the other shots.
    QList<Shot> insertDropped(const QList<Shot> &shots, QcInventoryAccess &access);
    // The filled item an empty one becomes when it takes `liquid`, or empty.
    QString filledBucket(QcItemId empty, QcBlockId liquid) const;
    // Applies the shots whose action needs a cell; returns those that fall back to throwing.
    QList<Shot> use(const QList<Shot> &shots, QcInventoryAccess &access);
    bool useOne(const Shot &shot, QcInventoryAccess &access);
    bool placeLiquid(const Shot &shot, QcInventoryAccess &access);
    bool scoopLiquid(const Shot &shot, QcInventoryAccess &access);
    bool equip(const Shot &shot, QcInventoryAccess &access);
    // Flint and steel lights the cell in front and wears by one (it breaks when used up).
    bool ignite(const Shot &shot, QcInventoryAccess &access);
    // Ammunition flies out as its projectile (decision E-621); false at the projectile limit.
    bool shoot(const Shot &shot, QcInventoryAccess &access);
    void throwItem(const Shot &shot, QcInventoryAccess &access);
    // The empty or filled item `name` takes the place of one unit in the dispenser's slot; what does
    // not fit is thrown out.
    void exchange(const Shot &shot, const QString &name, QcInventoryAccess &access);

    QcServerState &m_state;
    QcServerBlocks &m_blocks;
    QcServerSounds &m_sounds;
    const QcSignalTable &m_table;
    EquipPlayer m_equipPlayer;
    Ignite m_ignite;
};
