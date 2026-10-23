#include "server/villages/QcVillagePopulation.h"

#include "server/core/QcServerWorld.h"
#include "server/pathing/QcPathPlanner.h"
#include "server/villages/QcVillages.h"
#include "worldgen/terrain/QcNoise.h"
#include "worldgen/villages/QcVillagePlan.h"

#include <algorithm>
#include <cmath>

QcVillagePopulation::QcVillagePopulation(QcMobManager &mobs, std::shared_ptr<const QcContentRegistry> content, quint64 seed)
    : m_mobs(mobs)
    , m_content(std::move(content))
    , m_seed(seed)
    , m_relocation(m_content)
    , m_breeding(m_content)
    , m_golems(m_content)
{
    m_mobs.onDeparture([this](const QcCreature &mob, QcMobManager::Departure why) { depart(mob, why); });
}

void QcVillagePopulation::setVillagerLimits(QcVillagerHomes::Limits homes, QcVillagerBehaviour::Limits behaviour)
{
    m_villagerLimits = homes;
    m_mobs.villagers().setLimits(behaviour);
}

void QcVillagePopulation::noteChunk(QcServerWorld &world, QcChunkPos pos)
{
    checkVillageClaims(world, pos);
}

void QcVillagePopulation::settle(QcServerWorld &world, QcChunkPos pos, const NextId &nextEntityId, bool onlyAlwaysLivedIn)
{
    spawnVillagers(world, pos, nextEntityId, onlyAlwaysLivedIn);
    spawnWaitingVillagers(world, pos, nextEntityId, onlyAlwaysLivedIn);
}

void QcVillagePopulation::tick(QcServerWorld &world, double worldTime, const NextId &nextEntityId)
{
    if (--m_villageLifeIn <= 0) {
        m_villageLifeIn = villageLifeTicks;
        liveInVillages(world, worldTime, nextEntityId);
    }
}

void QcVillagePopulation::spawnVillagers(QcServerWorld &world, QcChunkPos pos, const NextId &nextEntityId, bool onlyAlwaysLivedIn)
{
    const std::shared_ptr<const QcVillagePlan> plan = world.generator().villageFor(pos);
    QcVillage *village = plan && (plan->alwaysLivedIn || !onlyAlwaysLivedIn) ? world.villages().find(plan->id) : nullptr;
    const std::optional<int> type = village ? m_content->villagerOfStyle(village->style) : std::nullopt;
    if (!type) {
        return;
    }
    const QcVillagerSettings &settings = m_content->villagerSettingsOf(*type);

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
        const std::optional<QcVec3> at = QcVillagerHomes::besideBed(world, m_mobs.paths().cells(), bed, rules);
        if (!at) {
            m_bedsWaiting.insert(pos);
            continue;
        }
        const auto look = static_cast<quint32>(qcPositionRandom(qcMixSeed(m_seed, 4100), bed.x, bed.y, bed.z) * 4294967295.0);
        m_mobs.spawn(*type, *at, nextEntityId());
        QcCreature &villager = m_mobs.editableMobs().last();
        *villager.villager = QcVillagerHomes::settle(*village, bed, villager.uid, settings, look);
        villager.confined = plan->residentsConfined;
        // A village always lived in (a row of the showcase world) shows a child in its last bed
        // (decision E-415); it grows up like any other.
        if (plan->alwaysLivedIn && bed == village->beds.constLast().at) {
            villager.growUp = settings.breeding.growUpSeconds;
        }
        ++population;
        world.villages().noteChanged();
    }
}

void QcVillagePopulation::spawnWaitingVillagers(QcServerWorld &world, QcChunkPos pos, const NextId &nextEntityId,
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

void QcVillagePopulation::checkVillageClaims(QcServerWorld &world, QcChunkPos pos)
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

bool QcVillagePopulation::claimHoldersLoaded(const QcServerWorld &world, const QcVillage &village)
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

void QcVillagePopulation::reconcileClaims(QcServerWorld &world, QcVillage &village)
{
    m_claimsChecked.insert(village.id);

    // Persistent ids are unique (decision E-420): who is there holds its claims, the others lapse.
    QSet<quint64> present;
    for (const QcCreature &mob : m_mobs.mobs()) {
        if (mob.isVillager() && mob.villager->village == village.id) {
            present.insert(mob.uid);
        }
    }
    // Its population is who is there.
    const bool changed = village.population != static_cast<int>(present.size());
    village.population = static_cast<int>(present.size());
    village.residents = {true, present};
    if (QcVillagerHomes::dropLostClaims(village, present) > 0 || changed) {
        world.villages().noteChanged();
    }
}

bool QcVillagePopulation::settled(QcServerWorld &world, QcVillage &village)
{
    if (m_claimsChecked.contains(village.id)) {
        return true;
    }
    const auto claimed = [](const QcVillage::Spot &spot) { return spot.claimedBy != 0; };
    if (village.population == 0 && std::ranges::none_of(village.beds, claimed) && std::ranges::none_of(village.workstations, claimed)) {
        m_claimsChecked.insert(village.id);
        return true;
    }
    if (claimHoldersLoaded(world, village)) {
        reconcileClaims(world, village);
        return true;
    }
    return false;
}

void QcVillagePopulation::relocate(QcServerWorld &world)
{
    QSet<qint64> unchecked;
    for (const QcCreature &mob : m_mobs.mobs()) {
        if (!mob.dormant && mob.isVillager() && mob.villager->village != 0 && !m_claimsChecked.contains(mob.villager->village)) {
            unchecked.insert(mob.villager->village);
        }
    }
    for (const qint64 id : std::as_const(unchecked)) {
        if (QcVillage *village = world.villages().find(id)) {
            settled(world, *village);
        }
    }

    const QcVillagerRelocation::Context context{world.villages(), m_villagerLimits,
                                                [this, &world](QcVillage &village) { return settled(world, village); }};
    m_relocation.check(m_mobs.editableMobs(), context);
}

void QcVillagePopulation::liveInVillages(QcServerWorld &world, double worldTime, const NextId &nextEntityId)
{
    relocate(world);

    const QcVillagerBehaviour &behaviour = m_mobs.villagers();
    const QcVillagerBreeding::Context breeding{world, world.villages(), worldTime, [&behaviour](QcBlockPos pos) { return behaviour.rainsAt(pos); },
                                               m_villagerLimits, &m_claimsChecked};
    const QList<QcVillagerBreeding::Birth> births = m_breeding.check(m_mobs.editableMobs(), breeding);
    for (const QcVillagerBreeding::Birth &birth : births) {
        QcVillage *village = world.villages().find(birth.village);
        const auto look = static_cast<quint32>(qcPositionRandom(qcMixSeed(m_seed, 4101), birth.bed.x,
            static_cast<int>(std::fmod(worldTime, 1e9)), birth.bed.z) * 4294967295.0);
        const QcVillagerSettings &settings = m_content->villagerSettingsOf(birth.type);
        m_mobs.spawn(birth.type, birth.position, nextEntityId());
        QcCreature &child = m_mobs.editableMobs().last();
        *child.villager = QcVillagerHomes::settle(*village, birth.bed, child.uid, settings, look);
        child.growUp = settings.breeding.growUpSeconds;
        // A child of a village whose residents are confined (a row of the showcase world) is too.
        const std::shared_ptr<const QcVillagePlan> plan = world.generator().villageFor(birth.bed.chunk());
        child.confined = plan && plan->id == birth.village && plan->residentsConfined;
        world.villages().noteChanged();
    }

    // Villagers summon golems also without the game rule mobSpawning, like they have children
    // (decision E-470); the server limit villageGolems (0: none) bounds them.
    QSet<qint64> lived;
    for (const QcCreature &mob : m_mobs.mobs()) {
        // Only villages whose claims were reconciled (decision E-416): before that, villagers in
        // chunks not loaded yet are not counted.
        if (!mob.dormant && mob.isVillager() && mob.villager->village != 0 && m_claimsChecked.contains(mob.villager->village)) {
            lived.insert(mob.villager->village);
        }
    }
    const QList<QcVillageGolems::Spawn> golems = m_golems.check(m_mobs.mobs(), lived, {world, m_mobs.paths().cells(), worldTime, m_seed, m_golemLimit});
    for (const QcVillageGolems::Spawn &golem : golems) {
        m_mobs.spawn(golem.type, golem.position, nextEntityId(), false, true);
    }
}

void QcVillagePopulation::depart(const QcCreature &mob, QcMobManager::Departure why)
{
    // A villager in a chunk that unloads rests there with its claims and its door (decision E-411).
    if (why == QcMobManager::Departure::Unloaded || !mob.isVillager()) {
        return;
    }
    if (m_villages) {
        QcVillagerHomes::release(*m_villages, *mob.villager, mob.uid);
    }
}
