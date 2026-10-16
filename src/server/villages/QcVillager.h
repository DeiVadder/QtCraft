#pragma once

#include "QcBlockPos.h"
#include "QcVec3.h"
#include "QcVillagerDefinitions.h"

#include <QString>

#include <optional>

// The state of one of a villager's offers (decision E-414), by the offer's id: how often it was
// traded since the last restock and how far demand raised its price.
struct QcVillagerOffer
{
    static constexpr int maxDemand = 64;

    QString id;
    int uses = 0;
    int demand = 0;

    bool operator==(const QcVillagerOffer &) const = default;
};

// What is saved of a villager (decisions E-411, E-414): the village it lives in and its name there
// (the `claimedBy` of its bed and workstation, QcVillage::Spot), its bed (head) and workstation, its
// profession with level and experience, the state of its offers and when it last restocked, the
// seed and village style of its look, and the crops it brought in (food for breeding, stage 37.3).
struct QcVillagerRecord
{
    static constexpr int maxLevel = 5;
    static constexpr int maxExperience = 1 << 20;
    static constexpr int maxProduce = 1 << 16;
    static constexpr int maxOffers = 32;
    // The latest world time a restock may lie at (about 30 million years of play); a saved time
    // beyond it is refused as damaged.
    static constexpr double maxRestockTime = 1e15;

    // QcVillage::id; 0 for a villager without a village (summoned), which only strolls.
    qint64 village = 0;
    QString name;
    std::optional<QcBlockPos> home;
    std::optional<QcBlockPos> work;
    QString profession;
    int level = 1;
    int experience = 0;
    // In the order of QcVillagerTrades::offersOf; aligned with it by QcVillagerTrades::sync.
    QList<QcVillagerOffer> offers;
    // World time of the last restock (negative: never) and the restocks on that day.
    double lastRestock = -1.0;
    int restocks = 0;
    quint32 look = 0;
    QString style;
    int produce = 0;

    bool operator==(const QcVillagerRecord &) const = default;
};

// A villager as it lives in the world: its record and what it is doing (not saved; a villager
// loaded from its chunk decides anew). QcVillagerBehaviour moves it, QcServerVillagers carries out
// what it does to the world.
struct QcVillager : QcVillagerRecord
{
    QcVillagerActivity activity = QcVillagerActivity::Wander;
    // Lying in its bed (QcMsg::Sleeping): the physics leaves it there.
    bool sleeping = false;
    // A door it opened on its way and closes once it is through.
    std::optional<QcBlockPos> openedDoor;
    // Where it is going (QcVillagerBehaviour::Goal) and how close counts as there; a stroll is
    // searched like a wanderer's walk. None: it stands.
    std::optional<QcBlockPos> target;
    int closeEnough = 1;
    bool strolling = false;
    // At its target (QcVillagerBehaviour::arrive): it stands instead of walking on.
    bool arrived = false;
    // On a long walk: the road cell it walks to first (QcVillagerRoads).
    std::optional<QcBlockPos> leg;
    // Ticks until it asks the roads again after they had no leg for it (QcVillagerBehaviour::legRetryTicks).
    int legRetryIn = 0;
    // Decisions until a farmer looks for ripe crops again after it found none
    // (QcVillagerBehaviour::cropLookDecisions).
    int cropLookIn = 0;
    // In water without a path to its target: the road cell on land it gets out to first
    // (QcVillagerRoads::wayOutOfWater).
    std::optional<QcBlockPos> shore;
    // A ripe crop a farmer walks to.
    std::optional<QcBlockPos> crop;
    // The place it runs from (a hostile creature) while the creature's fleeTimer runs.
    std::optional<QcVec3> threat;
    // The player (entity id) whose trading screen it has open, 0 for none (QcServerTrading), and
    // where that player stands: it stands facing them while the screen is open.
    quint32 trader = 0;
    QcVec3 traderAt;
    // Ticks until it decides again what to do (QcVillagerBehaviour::decideTicks).
    int decideIn = 0;
    // Its look changed (a new profession): clients get it again.
    bool lookChanged = false;
};
