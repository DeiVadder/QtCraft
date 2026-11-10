// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/dimensions/QcEntityTravel.h"

#include "server/dimensions/QcServerDimension.h"

namespace {

using Journey = QcEntityTravel::Journey;

bool moveCreature(const Journey &journey)
{
    QcMobManager &from = journey.from->entities().creatures();
    const QcCreature *mob = from.find(journey.id);
    if (mob == nullptr) {
        return false;
    }

    QcMobManager::Stored stored = QcMobManager::storedOf(*mob);
    stored.position = journey.position;
    const bool persistent = mob->persistent;
    const quint32 id = journey.id;
    from.unload([id](const QcCreature &candidate) { return candidate.id == id; });

    QcMobManager &to = journey.to->entities().creatures();
    to.restore({stored}, {}, [id] { return id; });
    // Restored creatures are kept with the world; one that was not stays as it was.
    if (QcCreature *arrived = to.find(id)) {
        arrived->persistent = persistent;
    }
    return true;
}

bool moveItem(const Journey &journey)
{
    QcItemEntityManager &from = journey.from->entities().items();
    const QList<QcItemEntity> &items = from.items();
    const auto it = std::ranges::find(items, journey.id, &QcItemEntity::id);
    if (it == items.end()) {
        return false;
    }

    const QcItemEntityManager::Drop drop{it->stack, journey.position, it->motion.velocity, it->pickupDelay, it->age, it->kept};
    const quint32 id = journey.id;
    from.removeWhere([id](const QcItemEntity &item) { return item.id == id; });
    // At the target's limit of active items spawn() queues the drop and returns 0: it appears
    // there once room frees up, so it has travelled all the same.
    journey.to->entities().items().spawn(drop, id);
    return true;
}

bool moveMinecart(const Journey &journey)
{
    QcMinecartManager &from = journey.from->entities().minecarts();
    std::optional<QcMinecart> cart = from.remove(journey.id);
    if (!cart) {
        return false;
    }

    QcMinecart travelling = *cart;
    travelling.rider = 0;
    travelling.rest.reset();
    travelling.motion.position = journey.position;
    if (!journey.to->entities().minecarts().add(travelling)) {
        // No room there: it stays where it was.
        from.add(*cart);
        return false;
    }
    return true;
}

} // namespace

namespace QcEntityTravel {

bool move(const Journey &journey)
{
    if (journey.from == nullptr || journey.to == nullptr || journey.from == journey.to) {
        return false;
    }

    switch (journey.kind) {
    case QcEntityKind::Creature:
        return moveCreature(journey);
    case QcEntityKind::Item:
        return moveItem(journey);
    case QcEntityKind::Minecart:
        return moveMinecart(journey);
    default:
        return false;
    }
}

} // namespace QcEntityTravel
