#include "server/guardians/QcGuardians.h"

#include "common/content/registry/QcSpawnerContent.h"
#include "common/physics/QcPhysics.h"
#include "server/core/QcServerState.h"
#include "server/guardians/QcGuardPost.h"

#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

QcGuardians::QcGuardians(QcServerState &state, const QcSpawnerRules &cells)
    : m_state(state)
    , m_cells(cells)
{
}

void QcGuardians::tick()
{
    if (m_state.clock().tick() % checkTicks != 0) {
        return;
    }
    const QList<QcVec3> players = livingPlayers();
    if (players.isEmpty()) {
        return;
    }

    // The zones near any player, each once, in the order of their ids (the same in every run).
    QList<QcGuardSite> sites;
    QSet<quint64> seen;
    for (const QcVec3 &player : players) {
        // Only planned regions: planning one takes milliseconds, it happens on a generation thread
        // and the zones there are served from a later check.
        const QcBlockPos from = QcPhysics::blockContaining(player);
        const std::optional<QList<QcGuardSite>> near = m_state.world().generator().plannedGuardSitesNear(from, reach);
        if (!near) {
            m_state.world().planGuardSitesInBackground(from, reach);
            continue;
        }
        for (const QcGuardSite &site : *near) {
            if (!seen.contains(site.id)) {
                seen.insert(site.id);
                sites.append(site);
            }
        }
    }
    std::ranges::sort(sites, {}, &QcGuardSite::id);

    int total = guardianCount();
    for (const QcGuardSite &site : std::as_const(sites)) {
        if (serve(site, players, total)) {
            ++total;
        }
    }
}

bool QcGuardians::serve(const QcGuardSite &site, const QList<QcVec3> &players, int total)
{
    const quint64 now = m_state.clock().tick();
    const quint64 cooldown = quint64(std::llround(site.rule.cooldownSeconds * QcPhysics::tickRate));
    if (guardianCount(site.id) >= site.rule.max) {
        m_readyAt.insert(site.id, now + cooldown);
        return false;
    }
    // A zone seen for the first time in this session waits half its cooldown: reloading the world
    // brings the next guardian at most that much earlier.
    const auto ready = m_readyAt.constFind(site.id);
    if (ready == m_readyAt.cend()) {
        m_readyAt.insert(site.id, now + cooldown / 2);
        return false;
    }
    if (now < *ready) {
        return false;
    }

    const std::optional<int> type = m_state.content().mobIndex(site.rule.mob);
    if (!type || !QcSpawnerContent::refusal(m_state.content().mobs()[*type]).isEmpty() || total >= m_state.config().limits.guardians) {
        return false;
    }
    if (!playersAllow(site, players) || !zoneLoaded(site)) {
        return false;
    }
    if (!bringForth(site, *type)) {
        return false;
    }
    m_readyAt.insert(site.id, now + cooldown);
    return true;
}

bool QcGuardians::playersAllow(const QcGuardSite &site, const QList<QcVec3> &players)
{
    const QcGuardPost zone{site.id, site.min, site.max};
    double nearest = std::numeric_limits<double>::max();
    for (const QcVec3 &player : players) {
        nearest = std::min(nearest, zone.distanceTo(player));
    }
    return nearest >= site.rule.minPlayerDistance && nearest <= site.rule.maxPlayerDistance;
}

bool QcGuardians::zoneLoaded(const QcGuardSite &site) const
{
    const QcChunkPos first = site.min.chunk();
    const QcChunkPos last = site.max.chunk();
    for (int cz = first.z; cz <= last.z; ++cz) {
        for (int cx = first.x; cx <= last.x; ++cx) {
            if (!m_state.world().chunk({cx, cz})) {
                return false;
            }
        }
    }
    return true;
}

bool QcGuardians::bringForth(const QcGuardSite &site, int type)
{
    QcSpawnerDefinition light;
    light.maxLight = site.rule.maxLight;
    QRandomGenerator &random = m_state.random();
    for (int attempt = 0; attempt < attempts; ++attempt) {
        const QcBlockPos feet{site.min.x + int(random.bounded(quint32(site.max.x - site.min.x + 1))),
                              site.min.y + int(random.bounded(quint32(site.max.y - site.min.y + 1))),
                              site.min.z + int(random.bounded(quint32(site.max.z - site.min.z + 1)))};
        if (!m_cells.fits(feet, light, type)) {
            continue;
        }
        QcMobManager &mobs = m_state.entities().creatures();
        mobs.spawn(type, {feet.x + 0.5, double(feet.y), feet.z + 0.5}, m_state.entities().newId(), true, false);
        mobs.editableMobs().last().guards = QcGuardPost{site.id, site.min, site.max};
        return true;
    }
    return false;
}

int QcGuardians::guardianCount() const
{
    return int(std::ranges::count_if(m_state.entities().creatures().mobs(), [](const QcCreature &mob) { return mob.guards.has_value(); }));
}

int QcGuardians::guardianCount(quint64 site) const
{
    return int(std::ranges::count_if(m_state.entities().creatures().mobs(), [site](const QcCreature &mob) { return mob.guards && mob.guards->site == site; }));
}

std::optional<quint64> QcGuardians::readyAt(quint64 site) const
{
    const auto it = m_readyAt.constFind(site);
    return it == m_readyAt.cend() ? std::nullopt : std::optional<quint64>(*it);
}

QList<QcVec3> QcGuardians::livingPlayers() const
{
    QList<QcVec3> positions;
    for (const auto &client : m_state.clients()) {
        if (client->state == QcServerClient::State::Playing && !client->dead) {
            positions.append(client->motion.position);
        }
    }
    return positions;
}
