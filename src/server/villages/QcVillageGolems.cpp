// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/villages/QcVillageGolems.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/physics/QcBodyFit.h"
#include "server/core/QcServerLogging.h"
#include "server/core/QcServerWorld.h"
#include "server/pathing/QcPathTerrain.h"
#include "server/villages/QcVillage.h"
#include "server/villages/QcVillages.h"
#include "worldgen/terrain/QcNoise.h"


#include <algorithm>
#include <cmath>
#include <map>

namespace {

// Salts of the draws that place a golem and decide whether gossip summons one.
constexpr quint64 placeSalt = 4200;
constexpr quint64 gossipSalt = 4300;

// The tick of a world time, for the draws.
int tickOf(double worldTime)
{
    return static_cast<int>(std::fmod(worldTime * 20.0, 1e9));
}

// Whether a moment lies within `seconds` before `now` (a moment after `now`: the time went back).
bool within(double moment, double seconds, double now)
{
    return moment >= 0.0 && moment <= now && now - moment <= seconds;
}

} // namespace

QcVillageGolems::QcVillageGolems(std::shared_ptr<const QcContentRegistry> content)
    : m_content(std::move(content))
{
}

std::optional<int> QcVillageGolems::kind(const QString &style) const
{
    const QString name = m_content->villageCatalog().golemOfStyle(style).mob;
    const std::optional<int> type = name.isEmpty() ? std::nullopt : m_content->mobIndex(name);
    if (!type && !name.isEmpty() && !m_missingLogged) {
        m_missingLogged = true;
        qCInfo(lcServer) << "Villagers summon no golems: the content has no creature" << name;
    }
    return type;
}

QcVillageGolems::Want QcVillageGolems::wants(const QcCreature &villager, const QcVillageGolemSettings &settings, double worldTime)
{
    if (!villager.isVillager() || villager.dormant || villager.noAi || villager.isYoung()) {
        return Want::None;
    }
    // Only a villager that slept in its bed lately wants one; panic makes it summon at once.
    const QcVillager &state = *villager.villager;
    if (!within(state.sleptAt, settings.sleptSeconds, worldTime)) {
        return Want::None;
    }
    return within(state.panickedAt, settings.panicSeconds, worldTime) ? Want::Panic : Want::Gossip;
}

QList<QcVillageGolems::Spawn> QcVillageGolems::check(const QList<QcCreature> &mobs, const QSet<qint64> &villages, const Context &context)
{
    // Every reconciled, loaded village counts its golems, wanted or not.
    for (const qint64 id : villages) {
        QcVillage *village = context.world.villages().find(id);
        const std::optional<int> type = village ? kind(village->style) : std::nullopt;
        if (type && loaded(context.world, *village)) {
            recount(*village, mobs, *type, context);
        }
    }

    forgetRestedChunks(context.worldTime);
    QHash<int, qint64> inWorld;
    QList<Spawn> spawns;
    for (Group &group : groups(mobs, villages, context)) {
        const QcVillage *village = group.village != 0 ? context.world.villages().find(group.village) : nullptr;
        const std::optional<int> type = kind(village ? village->style : QString());
        if (!type) {
            continue;
        }
        if (!inWorld.contains(*type)) {
            inWorld.insert(*type, golemsInWorld(mobs, *type, context.world.villages()));
        }
        std::ranges::sort(group.wanting, {}, &QcCreature::id);
        const QList<Spawn> summoned = summon(group, *type, mobs, context, context.worldLimit - inWorld.value(*type));
        spawns.append(summoned);
        inWorld[*type] += summoned.size();
    }
    return spawns;
}

QList<QcVillageGolems::Group> QcVillageGolems::groups(const QList<QcCreature> &mobs, const QSet<qint64> &villages, const Context &context) const
{
    // By village id (std::map), so the villages that summon at the world's limit do not depend on a
    // hash.
    std::map<qint64, Group> byVillage;
    for (const QcCreature &mob : mobs) {
        const qint64 id = mob.isVillager() ? mob.villager->village : 0;
        const QcVillage *village = id != 0 ? context.world.villages().find(id) : nullptr;
        if (!mob.isVillager() || (id != 0 && (!villages.contains(id) || !village))) {
            continue;
        }
        if (wants(mob, m_content->villageCatalog().golemOfStyle(village ? village->style : QString()), context.worldTime) != Want::None) {
            Group &group = byVillage[id];
            group.village = id;
            group.wanting.append(&mob);
        }
    }

    QList<Group> list;
    for (auto &entry : byVillage) {
        list.append(std::move(entry.second));
    }
    return list;
}

QList<QcVillageGolems::Spawn> QcVillageGolems::summon(const Group &group, int type, const QList<QcCreature> &mobs, const Context &context,
                                                     qint64 allowed)
{
    QcVillage *village = group.village != 0 ? context.world.villages().find(group.village) : nullptr;
    const QcVillageGolemSettings settings = m_content->villageCatalog().golemOfStyle(village ? village->style : QString());
    // Gossip summons with its chance once per check and village.
    const double roll = qcPositionRandom(qcMixSeed(context.seed, gossipSalt), static_cast<int>(group.village % 1000003), tickOf(context.worldTime), 0);
    QList<Spawn> spawns;
    for (const QcCreature *summoner : group.wanting) {
        if (spawns.size() >= allowed || (village && !spawns.isEmpty())) {
            break;
        }
        const QcVec3 at = summoner->motion.position;
        if (!calls(*summoner, group, settings, context.worldTime, roll) || !rested(village, settings, at, context.worldTime)) {
            continue;
        }

        // No golem of the kind near it (also none summoned in this check), and all of its
        // surroundings loaded (a golem may rest there).
        const auto near = [&](const QcVec3 &golem) { return (golem - at).length() <= settings.golemDistance; };
        const bool golemNear = std::ranges::any_of(mobs, [&](const QcCreature &mob) { return mob.type == type && near(mob.motion.position); })
                               || std::ranges::any_of(spawns, [&](const Spawn &spawn) { return near(spawn.position); });
        if (golemNear || !loadedAround(context.world, at, settings.golemDistance + settings.spawnRadius)) {
            continue;
        }
        const std::optional<QcVec3> place = placeNear(at, type, settings.spawnRadius, context, summoner->id);
        if (!place) {
            continue;
        }

        if (village) {
            village->golems += 1;
            village->lastGolem = context.worldTime;
            context.world.villages().noteChanged();
        } else {
            m_lastWithoutVillage.insert(QcPhysics::blockContaining(at).chunk(), context.worldTime);
        }
        spawns.append(Spawn{group.village, type, *place});
    }
    return spawns;
}

bool QcVillageGolems::calls(const QcCreature &summoner, const Group &group, const QcVillageGolemSettings &settings, double worldTime,
                            double roll)
{
    // Those of the group near it that slept lately, itself included (every one of the group did).
    const qsizetype gathered = std::ranges::count_if(group.wanting, [&](const QcCreature *other) {
        return (other->motion.position - summoner.motion.position).length() <= settings.gatherDistance;
    });
    const bool panic = wants(summoner, settings, worldTime) == Want::Panic && gathered >= settings.minVillagers;
    const bool gossip = gathered >= settings.gossipVillagers && roll < settings.gossipChance;
    return panic || gossip;
}

void QcVillageGolems::forgetRestedChunks(double worldTime)
{
    const double cooldown = m_content->villageCatalog().golemOfStyle(QString()).cooldownSeconds;
    m_lastWithoutVillage.removeIf([&](const auto &entry) { return entry.value() > worldTime || worldTime - entry.value() >= cooldown; });
}

bool QcVillageGolems::rested(const QcVillage *village, const QcVillageGolemSettings &settings, const QcVec3 &at, double worldTime) const
{
    if (village) {
        // A last golem after the world time: the time went back, so it is long ago.
        const double last = village->lastGolem;
        return last < 0.0 || last > worldTime || worldTime - last >= settings.cooldownSeconds;
    }

    // Without a village: no golem summoned from this chunk or one next to it within the pause.
    const QcChunkPos chunk = QcPhysics::blockContaining(at).chunk();
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            const double last = m_lastWithoutVillage.value({chunk.x + dx, chunk.z + dz}, -1.0);
            if (last >= 0.0 && last <= worldTime && worldTime - last < settings.cooldownSeconds) {
                return false;
            }
        }
    }
    return true;
}

void QcVillageGolems::recount(QcVillage &village, const QList<QcCreature> &mobs, int type, const Context &context)
{
    const int golems = golemsIn(village, mobs, type);
    // The world time went back (a command set it): the last golem counts as long ago.
    const bool timeWentBack = village.lastGolem > context.worldTime;
    if (golems != village.golems || timeWentBack) {
        village.golems = golems;
        village.lastGolem = timeWentBack ? -1.0 : village.lastGolem;
        context.world.villages().noteChanged();
    }
}

qint64 QcVillageGolems::golemsInWorld(const QList<QcCreature> &mobs, int type, const QcVillages &villages) const
{
    // Golems of `type` outside every village, and per village those in its area now or, for a
    // village whose golems are of `type`, those it counted last where that is more (its chunks may
    // not all be loaded). Golems of another kind that a village counted (its style changed) do not
    // count.
    qint64 outside = 0;
    QHash<qint64, int> byVillage;
    for (const QcCreature &mob : mobs) {
        if (mob.type != type) {
            continue;
        }
        if (const QcVillage *village = villages.at(QcPhysics::blockContaining(mob.motion.position))) {
            ++byVillage[village->id];
        } else {
            ++outside;
        }
    }
    return outside + villages.golems(byVillage, [this, type](const QcVillage &village) { return kind(village.style) == type; });
}

int QcVillageGolems::golemsIn(const QcVillage &village, const QList<QcCreature> &mobs, int type)
{
    return static_cast<int>(std::ranges::count_if(mobs, [&](const QcCreature &mob) {
        return mob.type == type && village.contains(QcPhysics::blockContaining(mob.motion.position));
    }));
}

bool QcVillageGolems::loaded(const QcServerWorld &world, const QcVillage &village)
{
    const QcChunkPos low = QcBlockPos{village.minX - QcVillage::reach, 0, village.minZ - QcVillage::reach}.chunk();
    const QcChunkPos high = QcBlockPos{village.maxX + QcVillage::reach, 0, village.maxZ + QcVillage::reach}.chunk();
    for (int x = low.x; x <= high.x; ++x) {
        for (int z = low.z; z <= high.z; ++z) {
            if (!world.chunk({x, z})) {
                return false;
            }
        }
    }
    return true;
}

bool QcVillageGolems::loadedAround(const QcServerWorld &world, const QcVec3 &at, double distance)
{
    const QcChunkPos low = QcPhysics::blockContaining(at - QcVec3{distance, 0.0, distance}).chunk();
    const QcChunkPos high = QcPhysics::blockContaining(at + QcVec3{distance, 0.0, distance}).chunk();
    for (int x = low.x; x <= high.x; ++x) {
        for (int z = low.z; z <= high.z; ++z) {
            if (!world.chunk({x, z})) {
                return false;
            }
        }
    }
    return true;
}

std::optional<QcVec3> QcVillageGolems::placeNear(const QcVec3 &around, int type, int radius, const Context &context, quint64 draw) const
{
    const QcBlockPos centre = QcPhysics::blockContaining(around);
    const int tick = tickOf(context.worldTime);
    const int span = 2 * radius + 1;
    for (int attempt = 0; attempt < placeAttempts; ++attempt) {
        const quint64 seed = qcMixSeed(qcMixSeed(context.seed, placeSalt + static_cast<quint64>(attempt)), draw);
        const int x = centre.x - radius + std::min(span - 1, static_cast<int>(qcPositionRandom(seed, centre.x, tick, centre.z) * span));
        const int z = centre.z - radius + std::min(span - 1, static_cast<int>(qcPositionRandom(seed, centre.z, tick, centre.x) * span));
        // From the top down, like the reference: the highest place of the column within reach.
        for (int dy = QcVillageGolemSettings::spawnHeight; dy >= -QcVillageGolemSettings::spawnHeight; --dy) {
            if (const std::optional<QcVec3> at = standsAt({x, centre.y + dy, z}, type, context)) {
                return at;
            }
        }
    }
    return std::nullopt;
}

bool QcVillageGolems::opaqueFloor(QcBlockPos column, const Context &context) const
{
    // A block in the feet's cell (a slab) is the floor, else the one below.
    for (const QcBlockPos cell : {column, column.offset(0, -1, 0)}) {
        const std::optional<QcBlockId> block = context.world.blockAt(cell);
        if (!block) {
            return false;
        }
        if (!m_content->state(*block).collision.isEmpty()) {
            return m_content->block(*block).renderLayer == QcRenderLayer::Opaque;
        }
    }
    return false;
}

std::optional<QcVec3> QcVillageGolems::standsAt(QcBlockPos feet, int type, const Context &context) const
{
    const QcMobDefinition &definition = m_content->mobs()[type];
    const QcPathRules rules = QcPathRules::of(definition);
    const QcPathDirectCells cells(context.world, context.cells);
    const QcPathStand stand = QcPathTerrain::standAt(cells, feet, rules);
    if (!stand.ok || stand.water || stand.hurts) {
        return std::nullopt;
    }

    // Not on glass, panes, leaves or ice: under every column of the body (a golem's feet cell is the
    // corner of four, decision E-909) a floor of the opaque render layer.
    const bool floored = QcPathTerrain::everyColumn(feet, rules, [&](QcBlockPos column) { return opaqueFloor(column, context); });
    if (!floored) {
        return std::nullopt;
    }

    // The whole body is clear in the middle of its columns.
    const QcVec3 at = QcPathTerrain::pointOf(feet, rules) + QcVec3{0.0, stand.lift(), 0.0};
    const QcPhysics::BodySize body{definition.width, definition.height};
    if (QcBodyFit::overlapsBlocks(context.world, at, body, QcBodyFit::Unloaded::Blocked)) {
        return std::nullopt;
    }
    return at;
}
