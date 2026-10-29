// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/blockrules/QcRails.h"
#include "common/core/QcAabb.h"
#include "common/world/QcBlockAccess.h"
#include "server/core/QcServerLimits.h"
#include "server/entities/QcMinecart.h"
#include "server/minecarts/QcMinecartMotion.h"

#include <QHash>
#include <QList>

#include <optional>

class QcContentRegistry;

// The minecarts of a world (decision E-382), the table of their kind in the entity register: adding,
// finding and removing them within the world's limit, and their movement in a tick: the carts meet
// each other and the living bodies (QcMinecartCollisions), then every awake cart moves on its own
// (QcMinecartMotion), and none ends deeper in a cart it overlapped. A cart at rest on a level rail
// (or held at the lower end of a slope) is not stepped until something moves it or its rail or the
// cells beyond its ends change.
class QcMinecartManager
{
public:
    // Carts in the world by default (QcServerLimits::maxMinecarts).
    static constexpr int maxCarts = static_cast<int>(QcServerLimits{}.maxMinecarts);

    // What a cart carries, from its item (QcItemDefinition::cartBlock, decision E-386).
    struct Load
    {
        // The carried block; nothing for a plain cart.
        std::optional<QcBlockId> block;
        // Slots of a container cart (its block's), 0 for none.
        int slotCount = 0;
        // A hopper cart (its block is a Signal Hopper).
        bool hopper = false;
    };

    explicit QcMinecartManager(const QcContentRegistry &content);

    static QcAabb bodyOf(const QcMinecart &cart);

    const QList<QcMinecart> &all() const { return m_carts; }
    // For moving carts from outside (pistons, riders); never add or remove through it.
    QList<QcMinecart> &editableAll() { return m_carts; }
    QcMinecart *find(quint32 id);
    const QcMinecart *find(quint32 id) const;
    // The cart the player (entity id) rides; null for none.
    QcMinecart *ridden(quint32 player);
    const QcMinecart *ridden(quint32 player) const;
    // False when the limit is reached.
    bool add(const QcMinecart &cart);
    std::optional<QcMinecart> remove(quint32 id);
    qsizetype limit() const { return m_maxCarts; }
    void setLimit(qsizetype limit) { m_maxCarts = limit; }
    // Adds the carts of a chunk that came back (decision E-374); returns how many were left out at the limit.
    qsizetype adopt(const QList<QcMinecart> &carts);
    const QcRails::Table &rails() const { return m_rails; }
    Load loadOf(const QcMinecart &cart) const { return m_loads.value(cart.item); }
    // A cart of `item` with empty slots for what it carries (no id, no place yet).
    QcMinecart fromItem(const QString &item) const;
    // Container carts whose feet lie in `cell` (a detector rail's or a hopper's pickup cell), by id.
    QList<const QcMinecart *> containersIn(QcBlockPos cell) const;

    // Pushes between carts and from `bodies` (boxes of living bodies that ride no cart), then moves
    // every awake cart by `dt` seconds. A cart on a powered activator rail stops pulling (hopper
    // carts) and is noted for activated(); on an unpowered one it pulls again.
    void tick(const QcBlockAccess &world, const QList<QcAabb> &bodies, double dt);
    // The carts (ids) that were on a powered activator rail in the latest tick (their riders get out).
    const QList<quint32> &activated() const { return m_activated; }
    // A tick without carts: nothing was activated.
    void forgetTick() { m_activated.clear(); }

private:
    // How a cart stands still at `position` on the rail at `rail`; nothing when no rail is there.
    std::optional<QcMinecart::Rest> restOf(QcBlockPos rail, const QcVec3 &position, const QcBlockAccess &world) const;
    // How a cart stands still at `position` on the ground off the rails.
    static QcMinecart::Rest groundRestOf(const QcVec3 &position, const QcBlockAccess &world);
    // Whether the cart rests where nothing changed since it came to rest (QcMinecart::Rest).
    bool isResting(const QcMinecart &cart, const QcVec3 &push, const QcBlockAccess &world) const;
    // Whether the cart, after its step, stands where it can rest: still on a level rail or held at
    // the lower end of a slope, and not on a powered rail that acts on standing carts; or still on
    // the ground off the rails, out of water.
    bool canRest(const QcMinecart &cart, const QcMinecartMotion::Result &result, const QcBlockAccess &world) const;

    QcRails::Table m_rails;
    QHash<QString, Load> m_loads;
    QList<quint32> m_activated;
    QList<QcMinecart> m_carts;
    qsizetype m_maxCarts = maxCarts;
};
