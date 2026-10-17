#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcChunkPos.h"
#include "server/core/QcServerLimits.h"
#include "server/mobs/QcMobManager.h"
#include "server/villages/QcVillageGolems.h"
#include "server/villages/QcVillagerBehaviour.h"
#include "server/villages/QcVillagerBreeding.h"
#include "server/villages/QcVillagerHomes.h"

#include <QSet>

#include <functional>
#include <memory>

class QcServerWorld;
struct QcVillage;
class QcVillages;

// The villagers of the villages among the creatures (decisions E-411, E-415, E-422): one moves in
// beside each free bed when a chunk of its village is seen for the first time, the claims of a
// village are reconciled once per run when all its villagers' chunks are loaded, every
// villageLifeTicks ticks villagers get children (QcVillagerBreeding) and villages iron golems
// (QcVillageGolems), and a villager that leaves the world for good (QcMobManager::Departure: died,
// removed, despawned, void) frees its claims. Part of QcMobManager, which owns the creatures and
// tells it when one leaves.
class QcVillagePopulation
{
public:
    // Village life runs every this many ticks.
    static constexpr int villageLifeTicks = 100;

    using NextId = std::function<quint32()>;

    // `mobs` owns the villagers and outlives this; the population listens to its departures.
    QcVillagePopulation(QcMobManager &mobs, std::shared_ptr<const QcContentRegistry> content, quint64 seed);

    // The village state villagers claim beds and workstations in (a villager that leaves frees its
    // claims there), how many may live and their budgets per tick.
    void setVillages(QcVillages *villages) { m_villages = villages; }
    void setVillagerLimits(QcVillagerHomes::Limits homes, QcVillagerBehaviour::Limits behaviour);
    // At most `golems` iron golems in the world (QcServerLimits villageGolems, only with natural
    // spawning).
    void setGolemLimit(qint64 golems) { m_golemLimit = golems; }
    const QcVillageGolems &golems() const { return m_golems; }

    // A chunk of a village arrived: the claims of the villages whose holders are all loaded now are
    // reconciled (checkVillageClaims).
    void noteChunk(QcServerWorld &world, QcChunkPos pos);
    // A villager for every free bed of a village whose head lies in the chunk, within the limits,
    // then the neighbours waiting for this chunk; with `onlyAlwaysLivedIn` only in a village that is
    // always lived in (QcVillagePlan::alwaysLivedIn).
    void settle(QcServerWorld &world, QcChunkPos pos, const NextId &nextEntityId, bool onlyAlwaysLivedIn);
    // Counts down to the next village life (children and golems).
    void tick(QcServerWorld &world, double worldTime, const NextId &nextEntityId);
    // Whether the village's claims were reconciled in this run (checkVillageClaims).
    bool claimsReconciled(qint64 village) const { return m_claimsChecked.contains(village); }

private:
    // A villager for every free bed of a village whose head lies in the chunk (decision E-411).
    void spawnVillagers(QcServerWorld &world, QcChunkPos pos, const NextId &nextEntityId, bool onlyAlwaysLivedIn);
    // The chunks next to `pos` with a free bed whose villager found no place beside it (in a
    // chunk not loaded then, m_bedsWaiting) try again now that `pos` is loaded.
    void spawnWaitingVillagers(QcServerWorld &world, QcChunkPos pos, const NextId &nextEntityId, bool onlyAlwaysLivedIn);
    // Once per village and run, when every chunk the villagers holding its claims may stand in is
    // loaded (around the arrived chunk `pos`): claims whose holder is not in the world lapse and the
    // population counts the villagers there (decision E-411).
    void checkVillageClaims(QcServerWorld &world, QcChunkPos pos);
    // Every chunk the villagers holding the village's claims may stand in is loaded.
    static bool claimHoldersLoaded(const QcServerWorld &world, const QcVillage &village);
    void reconcileClaims(QcServerWorld &world, QcVillage &village);
    // Children and golems of the villages, every villageLifeTicks ticks.
    void liveInVillages(QcServerWorld &world, double worldTime, const NextId &nextEntityId);
    // A creature left the world: a villager that left for good frees its claims and closes its door;
    // one whose chunk unloaded keeps both.
    void depart(const QcCreature &mob, QcMobManager::Departure why);

    QcMobManager &m_mobs;
    std::shared_ptr<const QcContentRegistry> m_content;
    quint64 m_seed = 0;
    QcVillages *m_villages = nullptr;
    QcVillagerHomes::Limits m_villagerLimits;
    // Chunks with a free bed whose villager found no place to stand beside it (spawnVillagers).
    QSet<QcChunkPos> m_bedsWaiting;
    // Villages whose claims checkVillageClaims() checked in this run.
    QSet<qint64> m_claimsChecked;
    QcVillagerBreeding m_breeding;
    QcVillageGolems m_golems;
    qint64 m_golemLimit = QcServerLimits{}.villageGolems;
    int m_villageLifeIn = villageLifeTicks;
};
