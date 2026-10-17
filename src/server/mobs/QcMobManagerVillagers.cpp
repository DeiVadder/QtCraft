// The villagers of QcMobManager (decision E-411): spawning them with the beds of their village,
// freeing their claims when they leave, their limits, their children and their villages' golems
// (decision E-415).

#include "server/mobs/QcMobManager.h"

#include "server/core/QcServerWorld.h"
#include "server/mobs/QcMobBehaviour.h"
#include "server/villages/QcVillages.h"
#include "worldgen/terrain/QcNoise.h"
#include "worldgen/villages/QcVillagePlan.h"

#include <cmath>

void QcMobManager::spawnVillagers(QcServerWorld &world, QcChunkPos pos, const std::function<quint32()> &nextEntityId, bool onlyAlwaysLivedIn)
{
    const QcVillagerSettings &settings = m_content->villagerSettings();
    const std::optional<int> type = m_content->mobIndex(settings.mob);
    const std::shared_ptr<const QcVillagePlan> plan = world.generator().villageFor(pos);
    QcVillage *village = plan && (plan->alwaysLivedIn || !onlyAlwaysLivedIn) ? world.villages().find(plan->id) : nullptr;
    if (!type || !m_content->mobs()[*type].villager || !village) {
        return;
    }

    // One villager beside each free bed, its look drawn from the bed's place: the same world gets
    // the same villagers.
    const QcPathRules rules = QcPathRules::of(m_content->mobs()[*type]);
    const QList<QcBlockPos> beds = QcVillagerHomes::freeBedsIn(*village, pos);
    qsizetype population = world.villages().population();
    for (const QcBlockPos &bed : beds) {
        if (village->population >= m_villagerLimits.perVillage || population >= m_villagerLimits.world) {
            break;
        }
        // No place beside the bed (the cells around it in a chunk not loaded yet): later.
        const std::optional<QcVec3> at = QcVillagerHomes::besideBed(world, paths().cells(), bed, rules);
        if (!at) {
            m_bedsWaiting.insert(pos);
            continue;
        }
        const auto look = quint32(qcPositionRandom(qcMixSeed(m_seed, 4100), bed.x, bed.y, bed.z) * 4294967295.0);
        spawn(*type, *at, nextEntityId());
        m_mobs.last().villager = QcVillagerHomes::settle(*village, bed, settings, look);
        // A village always lived in (a row of the showcase world) shows a child in its last bed
        // (decision E-415); it grows up like any other.
        if (plan->alwaysLivedIn && bed == village->beds.constLast().at) {
            m_mobs.last().growUp = settings.breeding.growUpSeconds;
        }
        ++population;
        world.villages().noteChanged();
    }
}

void QcMobManager::spawnWaitingVillagers(QcServerWorld &world, QcChunkPos pos, const std::function<quint32()> &nextEntityId,
                                         bool onlyAlwaysLivedIn)
{
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            const QcChunkPos next{pos.x + dx, pos.z + dz};
            if ((dx != 0 || dz != 0) && m_bedsWaiting.contains(next) && world.chunk(next)) {
                m_bedsWaiting.remove(next);
                spawnVillagers(world, next, nextEntityId, onlyAlwaysLivedIn);
            }
        }
    }
}

void QcMobManager::checkVillageClaims(QcServerWorld &world, QcChunkPos pos)
{
    if (!world.chunk(pos)) {
        return;
    }

    // The villages whose stray villagers may stand in `pos`: their area plus `reach` reaches into
    // it or, by up to strayReach blocks (at most one chunk), next to it. Their plans may lie in a
    // neighbouring region.
    const int strayChunks = (QcVillagerHomes::strayReach + QcWorld::chunkSize - 1) / QcWorld::chunkSize;
    const QList<qint64> ids = world.villages().near(pos, strayChunks);
    for (const qint64 id : ids) {
        QcVillage *village = m_claimsChecked.contains(id) ? nullptr : world.villages().find(id);
        if (village && claimHoldersLoaded(world, *village)) {
            reconcileClaims(world, *village);
        }
    }
}

bool QcMobManager::claimHoldersLoaded(const QcServerWorld &world, const QcVillage &village)
{
    // Villagers resting in a chunk not loaded yet still hold their claims: wait for all of them.
    const int around = QcVillage::reach + QcVillagerHomes::strayReach;
    const QcChunkPos low = QcBlockPos{village.minX - around, 0, village.minZ - around}.chunk();
    const QcChunkPos high = QcBlockPos{village.maxX + around, 0, village.maxZ + around}.chunk();
    for (int x = low.x; x <= high.x; ++x) {
        for (int z = low.z; z <= high.z; ++z) {
            if (!world.chunk({x, z})) {
                return false;
            }
        }
    }
    return true;
}

void QcMobManager::reconcileClaims(QcServerWorld &world, QcVillage &village)
{
    m_claimsChecked.insert(village.id);

    // A name twice (a villager settled under a name villages.json had not kept yet): the later one
    // gets a new name, and its claims lapse at its next decision.
    QSet<QString> present;
    bool changed = false;
    for (QcCreature &mob : m_mobs) {
        if (mob.villager.village != village.id || mob.villager.profession.isEmpty()) {
            continue;
        }
        if (present.contains(mob.villager.name)) {
            mob.villager.name = QcVillagerHomes::freshName(village);
            changed = true;
        }
        present.insert(mob.villager.name);
    }
    // Its population is who is there.
    changed = changed || village.population != int(present.size());
    village.population = int(present.size());
    if (QcVillagerHomes::dropLostClaims(village, present) > 0 || changed) {
        world.villages().noteChanged();
    }
}

void QcMobManager::liveInVillages(QcServerWorld &world, double worldTime, const std::function<quint32()> &nextEntityId)
{
    const QcVillagerSettings &settings = m_content->villagerSettings();
    const std::optional<int> type = m_content->mobIndex(settings.mob);
    if (!type || !m_content->mobs()[*type].villager) {
        return;
    }

    const QcVillagerBehaviour &behaviour = villagers();
    const QcVillagerBreeding::Context breeding{world, world.villages(), worldTime, [&behaviour](QcBlockPos pos) { return behaviour.rainsAt(pos); },
                                               m_villagerLimits, &m_claimsChecked};
    const QList<QcVillagerBreeding::Birth> births = m_breeding.check(m_mobs, breeding);
    for (const QcVillagerBreeding::Birth &birth : births) {
        QcVillage *village = world.villages().find(birth.village);
        const auto look = quint32(qcPositionRandom(qcMixSeed(m_seed, 4101), birth.bed.x, int(std::fmod(worldTime, 1e9)), birth.bed.z) * 4294967295.0);
        spawn(*type, birth.position, nextEntityId());
        m_mobs.last().villager = QcVillagerHomes::settle(*village, birth.bed, settings, look);
        m_mobs.last().growUp = settings.breeding.growUpSeconds;
        world.villages().noteChanged();
    }

    if (!m_naturalSpawning) {
        return;
    }
    QSet<qint64> lived;
    for (const QcCreature &mob : std::as_const(m_mobs)) {
        // Only villages whose claims were reconciled (decision E-416): before that, villagers in
        // chunks not loaded yet are not counted.
        if (!mob.dormant && mob.villager.village != 0 && !mob.villager.profession.isEmpty() && m_claimsChecked.contains(mob.villager.village)) {
            lived.insert(mob.villager.village);
        }
    }
    const QList<QcVillageGolems::Spawn> golems = m_golems.check(m_mobs, lived, {world, paths().cells(), worldTime, m_seed, m_golemLimit});
    for (const QcVillageGolems::Spawn &golem : golems) {
        spawn(golem.type, golem.position, nextEntityId(), false, true);
    }
}

void QcMobManager::depart(const QcCreature &mob)
{
    if (mob.villager.profession.isEmpty()) {
        return;
    }
    m_behaviour->villagers().leave(mob);
    if (m_villages) {
        QcVillagerHomes::release(*m_villages, mob.villager);
    }
}

void QcMobManager::setVillagerLimits(QcVillagerHomes::Limits homes, QcVillagerBehaviour::Limits behaviour)
{
    m_villagerLimits = homes;
    m_behaviour->villagers().setLimits(behaviour);
}

QcVillagerBehaviour &QcMobManager::villagers()
{
    return m_behaviour->villagers();
}

const QcVillagerBehaviour &QcMobManager::villagers() const
{
    return m_behaviour->villagers();
}
