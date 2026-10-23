#include "server/mobs/QcMobManager.h"

#include "common/core/QcWorldConstants.h"
#include "common/physics/QcPhysics.h"
#include "common/world/QcDayCycle.h"
#include "server/core/QcServerLogging.h"
#include "server/core/QcServerWorld.h"
#include "server/mobs/QcMobBehaviour.h"
#include "server/mobs/QcMobTeleport.h"
#include "server/mobs/QcMobUpkeep.h"
#include "server/spawning/QcNaturalSpawner.h"
#include "server/spawning/QcSpawnLight.h"
#include "server/villages/QcVillagePopulation.h"
#include "worldgen/terrain/QcNoise.h"

#include <QHash>

#include <algorithm>

namespace {

// The persistent ids of the creatures in the world.
QSet<quint64> loadedUids(const QList<QcCreature> &mobs)
{
    QSet<quint64> uids;
    for (const QcCreature &mob : mobs) {
        if (mob.persistent) {
            uids.insert(mob.uid);
        }
    }
    return uids;
}

} // namespace

QcMobManager::QcMobManager(std::shared_ptr<const QcContentRegistry> content, quint64 seed)
    : m_content(std::move(content))
    , m_seed(seed)
    , m_random(static_cast<quint32>(seed ^ (seed >> 32)))
    , m_despawnRandom(static_cast<quint32>(qcMixSeed(seed, 6001)))
    , m_yawRandom(&m_random)
    , m_behaviour(std::make_unique<QcMobBehaviour>(m_content, m_random))
    , m_spawner(std::make_unique<QcNaturalSpawner>(*this, m_content, seed, m_random))
    , m_population(std::make_unique<QcVillagePopulation>(*this, m_content, seed))
{
}

QcMobManager::~QcMobManager() = default;

void QcMobManager::setPathLimits(qint64 nodesPerTick, qint64 nodesPerSearch, qint64 pathLength)
{
    m_behaviour->setPathLimits(nodesPerTick, nodesPerSearch, pathLength);
}

const QcPathPlanner &QcMobManager::paths() const
{
    return m_behaviour->paths();
}

void QcMobManager::setCrowdLimits(qint64 throttled, qint64 idle)
{
    m_behaviour->setCrowdLimits(throttled, idle);
}

QList<QcMobCrowd::Crammed> QcMobManager::crammed(int limit) const
{
    return m_behaviour->crowd().crammed(limit);
}

const QcCreature *QcMobManager::find(quint32 id) const
{
    const auto it = std::ranges::find(m_mobs, id, &QcCreature::id);
    return it == m_mobs.end() ? nullptr : &*it;
}

QcCreature *QcMobManager::find(quint32 id)
{
    const auto it = std::ranges::find(m_mobs, id, &QcCreature::id);
    return it == m_mobs.end() ? nullptr : &*it;
}

quint32 QcMobManager::spawn(int type, const QcVec3 &position, quint32 id, bool underground, bool persistent)
{
    QcCreature mob;
    mob.underground = underground;
    mob.persistent = persistent;
    mob.id = id;
    mob.type = type;
    mob.motion.position = position;
    mob.health = m_content->mobs()[type].maxHealth;
    mob.yaw = static_cast<float>(m_yawRandom->bounded(360.0));
    // A villager always has a profession, the unemployed one at first (decision E-411); it lives
    // on with the world. Its look comes from its id, not from the random sequence of the creatures
    // (one moving in beside a bed gets the bed's look, spawnVillagers).
    if (m_content->mobs()[type].villager) {
        mob.villager.emplace();
        mob.villager->profession = m_content->villagerSettingsOf(type).unemployed;
        mob.villager->look = static_cast<quint32>(qcMixSeed(m_seed, 4101 + static_cast<quint64>(id)));
        mob.persistent = true;
    }
    if (m_content->mobs()[type].ai == QcMobDefinition::Ai::Archer) {
        mob.archer.emplace();
    }
    if (m_content->mobs()[type].ai == QcMobDefinition::Ai::Golem) {
        mob.golem.emplace();
    }
    // Only creatures that are saved have a persistent id (decision E-420).
    if (mob.persistent) {
        mob.uid = m_nextUid++;
    }
    m_mobs.append(std::move(mob));
    return id;
}

QcVec3 QcMobManager::knockbackVelocity(const QcVec3 &away, double strength, double lift)
{
    return away * (knockbackSpeed * strength) + QcVec3{0.0, knockbackLift * std::min(strength, 1.0) * lift, 0.0};
}

std::optional<QList<QcItemDrop>> QcMobManager::damage(const QcServerCombat::Blow &blow)
{
    const double amount = blow.damage;
    const auto it = std::ranges::find(m_mobs, blow.creature, &QcCreature::id);
    const QcHurtWindow::Take take = it == m_mobs.end() ? QcHurtWindow::Take::Ignored : it->hurt.wouldTake(amount);
    if (take == QcHurtWindow::Take::Ignored) {
        return std::nullopt;
    }

    // A stronger hit during the invulnerability deals the difference, without push or flash.
    QcCreature &mob = *it;
    const QcMobDefinition &definition = m_content->mobs()[mob.type];
    const double taken = mob.hurt.take(amount, m_hurtTicks);
    mob.health -= taken;
    mob.woundedThisTick = mob.woundedThisTick || (take == QcHurtWindow::Take::Fresh && taken > 0.0);
    mob.idleTicks = 0;
    const QcVec3 away = QcVec3{mob.motion.position.x - blow.from.x, 0.0, mob.motion.position.z - blow.from.z}.normalized();
    const double resisted = 1.0 - definition.golem.knockbackResistance;
    if (blow.attacker != 0) {
        mob.struckBy = blow.attacker;
        angerPack(mob, blow.attacker);
    }
    // A hurt pet stands up (like the reference, decision E-480).
    if (mob.isTame() && mob.tame->sitting) {
        mob.tame->sitting = false;
        mob.pose = QcPose::Standing;
    }
    if (take == QcHurtWindow::Take::Fresh) {
        mob.hurtThisTick = true;
        if (blow.knockback > 0.0 && resisted > 0.0) {
            mob.motion.velocity = knockbackVelocity(away, blow.knockback * resisted, blow.lift);
        }
    }

    // Golems do not run from blows; they strike back (QcGolemGrudges).
    if (!definition.hostile && definition.ai != QcMobDefinition::Ai::Golem) {
        mob.fleeTimer = QcPassiveBrain::fleeSeconds;
        mob.wanderDirection = away;
    }
    if (mob.health > 0.0) {
        return std::nullopt;
    }

    // Young animals leave nothing behind (decision E-293).
    QList<QcItemDrop> drops = mob.isYoung() ? QList<QcItemDrop>() : definition.drops;
    erase(it, Departure::Died);
    return drops;
}

void QcMobManager::angerPack(const QcCreature &mob, quint32 attacker)
{
    const double range = m_content->mobs()[mob.type].packRange;
    if (range <= 0.0 || mob.isTame()) {
        return;
    }
    // Its wild pack turns on the player as if each had been struck (QcNeutralBrain).
    for (QcCreature &other : m_mobs) {
        if (other.type == mob.type && other.id != mob.id && !other.dormant && !other.isTame()
            && (other.motion.position - mob.motion.position).length() <= range) {
            other.struckBy = attacker;
        }
    }
}

void QcMobManager::setHurtTicks(int ticks)
{
    if (ticks < m_hurtTicks) {
        for (QcCreature &mob : m_mobs) {
            mob.hurt.limit(ticks);
        }
    }
    m_hurtTicks = ticks;
}

bool QcMobManager::hurt(quint32 mobId, double amount)
{
    return wound(mobId, amount).has_value();
}

std::optional<QList<QcItemDrop>> QcMobManager::wound(quint32 mobId, double amount)
{
    const auto it = std::ranges::find(m_mobs, mobId, &QcCreature::id);
    if (it == m_mobs.end()) {
        return std::nullopt;
    }
    return woundAt(it - m_mobs.begin(), amount);
}

std::optional<QList<QcItemDrop>> QcMobManager::woundAt(qsizetype index, double amount)
{
    const auto it = m_mobs.begin() + index;
    const QcHurtWindow::Take take = it->hurt.wouldTake(amount);
    if (take == QcHurtWindow::Take::Ignored) {
        return std::nullopt;
    }
    const double taken = it->hurt.take(amount, m_hurtTicks);
    it->health -= taken;
    it->hurtThisTick = it->hurtThisTick || take == QcHurtWindow::Take::Fresh;
    it->woundedThisTick = it->woundedThisTick || (take == QcHurtWindow::Take::Fresh && taken > 0.0);
    if (it->health > 0.0) {
        return std::nullopt;
    }

    // Young animals leave nothing behind (decision E-293).
    QList<QcItemDrop> drops = it->isYoung() ? QList<QcItemDrop>() : m_content->mobs()[it->type].drops;
    erase(it, Departure::Died);
    return drops;
}

bool QcMobManager::remove(quint32 mobId)
{
    const auto it = std::ranges::find(m_mobs, mobId, &QcCreature::id);
    if (it == m_mobs.end()) {
        return false;
    }
    erase(it, Departure::Removed);
    return true;
}

void QcMobManager::unload(const std::function<bool(const QcCreature &mob)> &leaves)
{
    departWhere([&](const QcCreature &mob) { return leaves(mob) ? std::optional(Departure::Unloaded) : std::nullopt; });
}

QcMobManager::DepartureToken QcMobManager::onDeparture(DepartureListener listener)
{
    const DepartureToken token{m_nextDepartureToken++};
    m_departureListeners.append({token, std::move(listener)});
    return token;
}

void QcMobManager::removeDepartureListener(DepartureToken token)
{
    m_departureListeners.removeIf([token](const auto &entry) { return entry.first == token; });
}

void QcMobManager::depart(const QcCreature &mob, Departure why)
{
    // Its brain lets go first (a villager's open door); one whose chunk unloads rests with it.
    if (why != Departure::Unloaded) {
        m_behaviour->leave(mob);
    }

    // A copy (shared, not copied unless a listener unsubscribes meanwhile): the list may change.
    const QList<std::pair<DepartureToken, DepartureListener>> listeners = m_departureListeners;
    for (const auto &[token, listener] : listeners) {
        listener(mob, why);
    }
}

void QcMobManager::erase(QList<QcCreature>::iterator it, Departure why)
{
    const qsizetype index = it - m_mobs.begin();
    depart(m_mobs.at(index), why);
    m_mobs.removeAt(index);
}

void QcMobManager::departWhere(const std::function<std::optional<Departure>(const QcCreature &mob)> &leaves)
{
    QList<std::pair<qsizetype, Departure>> leaving;
    for (qsizetype i = 0; i < m_mobs.size(); ++i) {
        if (const std::optional<Departure> why = leaves(m_mobs.at(i))) {
            leaving.append({i, *why});
        }
    }
    if (leaving.isEmpty()) {
        return;
    }

    for (const auto &[index, why] : std::as_const(leaving)) {
        depart(m_mobs.at(index), why);
    }

    // Erase them in one pass, keeping the order of the others.
    qsizetype kept = 0;
    qsizetype next = 0;
    for (qsizetype i = 0; i < m_mobs.size(); ++i) {
        if (next < leaving.size() && leaving.at(next).first == i) {
            ++next;
            continue;
        }
        if (kept != i) {
            m_mobs[kept] = std::move(m_mobs[i]);
        }
        ++kept;
    }
    m_mobs.remove(kept, m_mobs.size() - kept);
}

QList<QcProjectileManager::Launch> QcMobManager::takeLaunches()
{
    return m_behaviour->takeLaunches();
}

QList<QcMobManager::CreatureHit> QcMobManager::takeCreatureHits()
{
    return m_behaviour->takeCreatureHits();
}

QList<QcMobManager::BlockAction> QcMobManager::takeBlockActions()
{
    return m_behaviour->takeBlockActions();
}

void QcMobManager::setGriefing(bool on)
{
    m_behaviour->setGriefing(on);
}

bool QcMobManager::teleport(quint32 mobId, const QcBlockAccess &world)
{
    QcCreature *mob = find(mobId);
    return mob && QcMobTeleport::teleport(*mob, m_content->mobs()[mob->type], world, m_random);
}

void QcMobManager::keep(QcCreature &mob, bool kept)
{
    // A named creature is always kept (decision E-866), whoever lets it go (a family, a released
    // child, a carried block put down).
    const bool keeps = kept || !mob.name.isEmpty();

    // A guardian belongs to its zone, not to the world: its post is not saved, and a kept one would
    // come back without it beside the one the zone brings (decision E-591). A name tag first takes
    // it out of its zone (QcNameTags::use, decision E-873); a name set otherwise keeps it a guardian.
    if (keeps && mob.guards) {
        return;
    }
    if (keeps && !mob.persistent) {
        mob.persistent = true;
        mob.uid = m_nextUid++;
    } else if (!keeps && mob.persistent) {
        mob.persistent = false;
        mob.uid = 0;
    }

    // A family is kept together (decision E-839): its children with the parent, under its id.
    if (!m_content->mobs()[mob.type].family) {
        return;
    }
    for (QcCreature &child : m_mobs) {
        if (child.parent == mob.id && child.id != mob.id) {
            keep(child, mob.persistent);
            child.parentUid = mob.persistent ? mob.uid : 0;
        }
    }
}

void QcMobManager::relinkFamilies()
{
    // Most loads bring no waiting child.
    if (std::ranges::none_of(m_mobs, [](const QcCreature &mob) { return mob.parentUid != 0 && mob.parent == 0; })) {
        return;
    }

    // The kept parents by their persistent id: entity id and most children.
    QHash<quint64, std::pair<quint32, int>> parents;
    QHash<quint32, int> linked;
    for (const QcCreature &mob : std::as_const(m_mobs)) {
        const std::optional<QcFamilyDefinition> &family = m_content->mobs()[mob.type].family;
        if (mob.persistent && family) {
            parents.insert(mob.uid, {mob.id, family->maxChildren});
        }
        if (mob.parent != 0) {
            ++linked[mob.parent];
        }
    }
    for (QcCreature &mob : m_mobs) {
        if (mob.parentUid == 0 || mob.parent != 0) {
            continue;
        }
        const auto [parent, most] = parents.value(mob.parentUid, {0, 0});
        if (parent == 0) {
            continue;
        }

        // A family never grows beyond its children at once (a world copied by hand, a changed mod):
        // the extra ones go on alone.
        if (linked.value(parent) >= most) {
            release(mob);
            continue;
        }
        mob.parent = parent;
        ++linked[parent];
    }
}

void QcMobManager::release(QcCreature &child)
{
    keep(child, false);
    child.parent = 0;
    child.parentUid = 0;
    child.parentChunk.reset();
}

quint64 QcMobManager::uniqueUid(quint64 saved, const QSet<quint64> &taken)
{
    if (!taken.contains(saved)) {
        return saved;
    }

    const quint64 uid = m_nextUid++;
    qCWarning(lcServer).noquote() << QStringLiteral("A loaded creature has the persistent id %1 of another one; it gets %2").arg(saved).arg(uid);
    return uid;
}

void QcMobManager::carryBlock(QcCreature &mob, QcBlockId block) const
{
    // A state the content no longer has (a mod was removed) is no block to carry.
    const bool valid = block != qcAirBlockId && m_content->isValidBlock(block);
    mob.carried = valid ? block : qcAirBlockId;
    mob.carriedItem = valid ? m_content->itemId(m_content->block(block).name).value_or(qcNoItem) : qcNoItem;
}

QList<QcMobManager::PlayerHit> QcMobManager::tick(QcServerWorld &world, const QList<PlayerView> &players, double worldTime,
                                                 double dt, const std::function<quint32()> &nextEntityId)
{
    for (QcCreature &mob : m_mobs) {
        mob.hurtThisTick = false;
        mob.woundedThisTick = false;
        mob.attackedThisTick = false;
    }
    const QList<PlayerHit> hits = m_behaviour->tick(m_mobs, world, players, {worldTime, dt});

    // Despawn mobs far from everyone, now and then those nobody came near for a while, and night
    // creatures in daylight once nobody is close.
    const bool night = QcDayCycle::isNight(worldTime);
    QList<QcMobUpkeep::Fate> fates;
    fates.reserve(m_mobs.size());
    for (QcCreature &mob : m_mobs) {
        const QcMobUpkeep::Upkeep upkeep = QcMobUpkeep::upkeepOf(mob, m_content->mobs()[mob.type], players, night);
        mob.idleTicks = upkeep.idleTicks;
        fates.append(upkeep.fate);
    }
    // departWhere asks for every creature once, in store order. The chance draws from a sequence of
    // its own, so idle creatures leave the creatures' sequence as it was.
    qsizetype next = 0;
    departWhere([&](const QcCreature &mob) -> std::optional<Departure> {
        const QcMobUpkeep::Fate fate = fates.at(next++);
        if (fate == QcMobUpkeep::Fate::Stays
            || (fate == QcMobUpkeep::Fate::MayDespawn && m_despawnRandom.bounded(QcMobUpkeep::idleChance) != 0)) {
            return std::nullopt;
        }
        return QcWorld::isInVoid(mob.motion.position.y) ? Departure::Void : Departure::Despawned;
    });

    m_spawner->tick(world, players, {night, QcSpawnLight::skyDarkening(worldTime, m_rain, m_thunder)}, dt, nextEntityId);
    m_population->tick(world, worldTime, nextEntityId);
    return hits;
}

void QcMobManager::populateChunk(QcServerWorld &world, QcChunkPos pos, const std::function<quint32()> &nextEntityId)
{
    // Chunks seen while creatures do not spawn get their animals when they load again later; a
    // village that is always lived in (the showcase's rows) gets residents for its free beds anyway,
    // each time a chunk of it arrives.
    m_population->noteChunk(world, pos);
    if (!m_naturalSpawning) {
        if (world.chunk(pos)) {
            m_population->settle(world, pos, nextEntityId, true);
        }
        return;
    }
    if (m_populated.contains(pos)) {
        return;
    }
    const std::shared_ptr<const QcChunk> chunk = world.chunk(pos);
    if (!chunk) {
        return;
    }

    m_populated.insert(pos);
    m_spawner->spawnVillageMobs(world, pos, nextEntityId);
    m_population->settle(world, pos, nextEntityId, false);
    m_spawner->spawnChunkAnimals(world, pos, *chunk, nextEntityId);
}

QcMobManager::Stored QcMobManager::storedOf(const QcCreature &mob)
{
    const std::optional<QcVillagerRecord> villager =
        mob.isVillager() ? std::optional<QcVillagerRecord>(*mob.villager) : std::nullopt;
    Stored stored;
    stored.type = mob.type;
    stored.position = mob.motion.position;
    stored.yaw = mob.yaw;
    stored.health = mob.health;
    stored.breedCooldown = mob.breedCooldown;
    stored.growUp = mob.growUp;
    stored.bred = mob.bred;
    stored.villager = villager;
    stored.uid = mob.uid;
    if (mob.effects) {
        stored.effects = mob.effects->all();
    }
    stored.layTimer = mob.layTimer;
    stored.kept = mob.kept;
    stored.playerBuilt = mob.golem && mob.golem->playerBuilt;
    stored.carried = mob.carried;
    stored.keptForBlock = mob.keptForBlock && mob.carried != qcAirBlockId;
    stored.confined = mob.confined;
    stored.noAi = mob.noAi;
    if (mob.isTame()) {
        stored.tame = QcTameState{.owner = mob.tame->owner, .sitting = mob.tame->sitting, .collar = mob.tame->collar};
    }
    stored.parentUid = mob.parentUid;
    stored.parentChunk = mob.parentChunk;
    stored.name = mob.name;
    return stored;
}

QList<QcMobManager::Stored> QcMobManager::persistentMobs() const
{
    QList<Stored> stored;
    for (const QcCreature &mob : m_mobs) {
        if (mob.persistent) {
            stored.append(storedOf(mob));
        }
    }
    return stored;
}

void QcMobManager::restore(const QList<Stored> &mobs, const QSet<QcChunkPos> &populated, const std::function<quint32()> &nextEntityId)
{
    m_populated.unite(populated);
    QSet<quint64> uids = loadedUids(m_mobs);
    for (const Stored &stored : mobs) {
        if (stored.type < 0 || stored.type >= m_content->mobs().size()) {
            continue;
        }

        QcCreature mob;
        mob.id = nextEntityId();
        mob.type = stored.type;
        mob.motion.position = stored.position;
        mob.yaw = stored.yaw;
        mob.health = std::clamp(stored.health, 0.1, m_content->mobs()[stored.type].maxHealth);
        mob.persistent = true;
        mob.dormant = true;
        mob.breedCooldown = std::max(0.0, stored.breedCooldown);
        mob.growUp = std::max(0.0, stored.growUp);
        mob.bred = stored.bred;
        mob.kept = stored.kept;
        if (!stored.effects.isEmpty()) {
            mob.effects.emplace().restore(stored.effects);
        }
        mob.layTimer = stored.layTimer;
        mob.parentUid = stored.parentUid;
        mob.parentChunk = stored.parentChunk;
        mob.confined = stored.confined;
        mob.noAi = stored.noAi;
        mob.name = stored.name;
        if (stored.tame && !stored.tame->owner.isEmpty()) {
            mob.tame.emplace(QcTameState{.owner = stored.tame->owner, .sitting = stored.tame->sitting, .collar = stored.tame->collar});
            mob.pose = stored.tame->sitting ? QcPose::Sitting : QcPose::Standing;
        }
        carryBlock(mob, stored.carried);
        mob.keptForBlock = stored.keptForBlock && mob.carried != qcAirBlockId;
        // The counter is saved with the chunks in one journal, so it lies beyond every saved id; this
        // only guards against a world.json put back by hand.
        m_nextUid = std::max(m_nextUid, stored.uid + 1);
        mob.uid = uniqueUid(stored.uid, uids);
        uids.insert(mob.uid);
        if (m_content->mobs()[stored.type].ai == QcMobDefinition::Ai::Archer) {
            mob.archer.emplace();
        }
        if (m_content->mobs()[stored.type].ai == QcMobDefinition::Ai::Golem) {
            mob.golem.emplace().playerBuilt = stored.playerBuilt;
        }
        if (m_content->mobs()[stored.type].villager) {
            static_cast<QcVillagerRecord &>(mob.villager.emplace()) = stored.villager.value_or(QcVillagerRecord{});
            // A profession the content no longer has (a mod was removed): unemployed.
            const QcVillagerSettings &species = m_content->villagerSettingsOf(stored.type);
            if (!species.profession(mob.villager->profession)) {
                mob.villager->profession = species.unemployed;
            }
        }
        m_mobs.append(std::move(mob));
    }
    relinkFamilies();
}

qsizetype QcMobManager::awakeCount() const
{
    return std::ranges::count_if(m_mobs, [](const QcCreature &mob) { return !mob.dormant; });
}

qsizetype QcMobManager::awakeSummonedCount() const
{
    return std::ranges::count_if(m_mobs, [](const QcCreature &mob) { return mob.summoned && !mob.dormant; });
}

QcVillagerBehaviour &QcMobManager::villagers()
{
    return m_behaviour->villagers();
}

const QcVillagerBehaviour &QcMobManager::villagers() const
{
    return m_behaviour->villagers();
}
