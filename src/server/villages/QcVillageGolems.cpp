#include "server/villages/QcVillageGolems.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/physics/QcBodyFit.h"
#include "server/core/QcServerWorld.h"
#include "server/pathing/QcPathTerrain.h"
#include "server/villages/QcVillage.h"
#include "server/villages/QcVillages.h"
#include "worldgen/terrain/QcNoise.h"

#include <QLoggingCategory>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

Q_LOGGING_CATEGORY(lcGolems, "qtcraft.server")

// Salt of the draws that place a golem.
constexpr quint64 placeSalt = 4200;

} // namespace

QcVillageGolems::QcVillageGolems(std::shared_ptr<const QcContentRegistry> content)
    : m_content(std::move(content))
{
}

std::optional<int> QcVillageGolems::kind(const QString &style) const
{
    const QString name = m_content->golemOfStyle(style).mob;
    const std::optional<int> type = name.isEmpty() ? std::nullopt : m_content->mobIndex(name);
    if (!type && !name.isEmpty() && !m_missingLogged) {
        m_missingLogged = true;
        qCInfo(lcGolems) << "Villages spawn no golems: the content has no creature" << name;
    }
    return type;
}

QList<QcVillageGolems::Spawn> QcVillageGolems::check(const QList<QcCreature> &mobs, const QSet<qint64> &villages, const Context &context) const
{
    // By id, so the villages that spawn at the world's limit do not depend on the set's order.
    QList<qint64> ids(villages.cbegin(), villages.cend());
    std::ranges::sort(ids);
    QList<std::pair<QcVillage *, int>> counted;
    for (const qint64 id : std::as_const(ids)) {
        QcVillage *village = context.world.villages().find(id);
        const std::optional<int> type = village ? kind(village->style) : std::nullopt;
        if (type && loaded(context.world, *village)) {
            recount(*village, mobs, *type, context);
            counted.append({village, *type});
        }
    }

    // Golems in the world by kind.
    QHash<int, qint64> inWorld;
    QList<Spawn> spawns;
    for (const auto &[village, type] : std::as_const(counted)) {
        if (!inWorld.contains(type)) {
            inWorld.insert(type, golemsInWorld(mobs, type, context.world.villages()));
        }
        if (inWorld.value(type) >= context.worldLimit || !due(*village, village->golems, context.worldTime)) {
            continue;
        }
        if (const std::optional<QcVec3> at = placeFor(*village, type, context)) {
            spawns.append({village->id, type, *at});
            village->golems += 1;
            village->lastGolem = context.worldTime;
            context.world.villages().noteChanged();
            ++inWorld[type];
        }
    }
    return spawns;
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
    return int(std::ranges::count_if(mobs, [&](const QcCreature &mob) {
        return mob.type == type && village.contains(QcPhysics::blockContaining(mob.motion.position));
    }));
}

bool QcVillageGolems::due(const QcVillage &village, int golems, double worldTime) const
{
    const QcVillageGolemSettings settings = m_content->golemOfStyle(village.style);
    // A last golem in the future: the world time went back, so it is long ago.
    const bool rested = village.lastGolem < 0.0 || village.lastGolem > worldTime || worldTime - village.lastGolem >= settings.cooldownSeconds;
    return village.population >= settings.minVillagers && golems < village.population / settings.villagersPerGolem && rested;
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

std::optional<QcVec3> QcVillageGolems::placeFor(const QcVillage &village, int type, const Context &context) const
{
    const int radius = m_content->golemOfStyle(village.style).spawnRadius;
    const auto tick = int(std::fmod(context.worldTime * 20.0, 1e9));
    for (int attempt = 0; attempt < placeAttempts; ++attempt) {
        const quint64 seed = qcMixSeed(context.seed, placeSalt + quint64(attempt));
        const double angle = qcPositionRandom(seed, village.centre.x, tick, village.centre.z) * 2.0 * std::numbers::pi;
        const double distance = 2.0 + qcPositionRandom(seed, village.centre.z, tick, village.centre.x) * (radius - 2.0);
        const int x = village.centre.x + int(std::lround(std::sin(angle) * distance));
        const int z = village.centre.z + int(std::lround(std::cos(angle) * distance));
        // From the meeting point's height outwards (0, +1, -1, +2, ...): the ground before roofs.
        for (int step = 0; step <= 2 * placeHeight; ++step) {
            const int dy = step % 2 == 1 ? (step + 1) / 2 : -step / 2;
            if (const std::optional<QcVec3> at = standsAt({x, village.centre.y + dy, z}, type, context)) {
                return at;
            }
        }
    }
    return std::nullopt;
}

std::optional<QcVec3> QcVillageGolems::standsAt(QcBlockPos feet, int type, const Context &context) const
{
    const QcMobDefinition &definition = m_content->mobs()[type];
    const QcPathDirectCells cells(context.world, context.cells);
    const QcPathStand stand = QcPathTerrain::standAt(cells, feet, QcPathRules::of(definition));
    if (!stand.ok || stand.water || stand.hurts) {
        return std::nullopt;
    }

    // The whole body is clear, not only the column of its feet (a golem is wider than a block).
    const QcVec3 at{feet.x + 0.5, feet.y + stand.lift(), feet.z + 0.5};
    const QcPhysics::BodySize body{definition.width, definition.height};
    if (QcBodyFit::overlapsBlocks(context.world, at, body, QcBodyFit::Unloaded::Blocked)) {
        return std::nullopt;
    }
    return at;
}
