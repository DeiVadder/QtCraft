#include "server/core/QcServerDebug.h"

#include "server/blocks/QcBlockUpdateQueue.h"
#include "server/chunks/QcChunkLoading.h"
#include "server/core/QcServerClient.h"
#include "server/core/QcServerState.h"
#include "server/tick/QcTickPhaseTimer.h"
#include "server/tick/QcTickScheduler.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

// Where a wandering or fleeing creature is heading: a few metres along its direction.
constexpr double goalLookAhead = 4.0;

} // namespace

QcServerDebug::QcServerDebug(const QcServerState &state, const QcBlockUpdateQueue &updates, const QcChunkLoading &loading,
                             const QcTickPhaseTimer &phases, const QcTickScheduler &ticks)
    : m_state(state)
    , m_loading(loading)
    , m_updates(updates)
    , m_phases(phases)
    , m_ticks(ticks)
{
}

void QcServerDebug::subscribe(QcServerClient &client, quint8 flags) const
{
    // Server-wide counts and the positions of chased players are for operators (decision E-225).
    if (m_state.permissionOf(client) == QcPermission::Player) {
        return;
    }
    client.view.debugFlags = flags & QcMsg::DebugSubscribe::all;
}

void QcServerDebug::update(QcServerClient &client) const
{
    if (client.view.debugFlags == 0 || (client.view.debugSentTick && m_state.clock().tick() - *client.view.debugSentTick < intervalTicks)) {
        return;
    }

    client.connection->send(build(client));
    client.view.debugSentTick = m_state.clock().tick();
}

QcMsg::ServerDebug QcServerDebug::build(const QcServerClient &client) const
{
    QcMsg::ServerDebug message;
    const QcPhaseWindow &window = m_phases.window();
    for (int phase = 0; phase < QcTickPhases::count; ++phase) {
        message.phaseMeanMs[static_cast<std::size_t>(phase)] = static_cast<float>(window.mean(phase));
        message.phaseMaxMs[static_cast<std::size_t>(phase)] = static_cast<float>(window.max(phase));
    }
    message.tickMeanMs = static_cast<float>(window.mean(QcTickPhaseTimer::durationSeries));
    message.tickMaxMs = static_cast<float>(window.max(QcTickPhaseTimer::durationSeries));
    message.lateMeanMs = static_cast<float>(window.mean(QcTickPhaseTimer::lateSeries));
    message.lateMaxMs = static_cast<float>(window.max(QcTickPhaseTimer::lateSeries));
    const quint64 since = client.view.debugSentTick ? m_state.clock().tick() - *client.view.debugSentTick : intervalTicks;
    message.recentTickMs = window.history(QcTickPhaseTimer::durationSeries, static_cast<int>(std::min<quint64>(since, QcMsg::ServerDebug::maxRecentTicks)));

    message.tick = m_state.clock().tick();
    message.skippedTicks = m_ticks.stats().skipped();
    message.loadedChunks = static_cast<quint32>(m_state.world().loadedChunkCount());
    message.players = static_cast<quint32>(std::ranges::count_if(m_state.clients(), [](const auto &other) { return other->joined; }));
    message.mobs = static_cast<quint32>(m_state.entities().creatures().awakeCount());
    message.items = static_cast<quint32>(m_state.entities().items().items().size());
    message.fallingBlocks = static_cast<quint32>(m_state.entities().fallingBlocks().all().size());
    message.pendingBlockUpdates = static_cast<quint32>(m_updates.pendingPlayerCount() + m_updates.pendingCommandCount());
    message.waitingBlockUpdates = static_cast<quint32>(m_updates.waitingCount());
    const QcVec3 &feet = client.motion.position;
    message.biome = m_state.world().generator().biomeAt(static_cast<int>(std::floor(feet.x)),
        static_cast<int>(std::floor(feet.z))).left(QcMsg::ServerDebug::maxTextLength);
    message.worldType = m_state.config().generator.left(QcMsg::ServerDebug::maxTextLength);

    message.chunks = chunksOf(client);
    if ((client.view.debugFlags & QcMsg::DebugSubscribe::mobGoals) != 0) {
        message.goals = goalsNear(client);
    }
    return message;
}

QcMsg::ServerDebug::Chunks QcServerDebug::chunksOf(const QcServerClient &client) const
{
    const QcChunkLoading::Numbers numbers = m_loading.numbers();
    QcMsg::ServerDebug::Chunks chunks;
    chunks.stored = static_cast<quint32>(numbers.stored);
    chunks.arriving = static_cast<quint32>(numbers.waiting);
    chunks.unsaved = static_cast<quint32>(numbers.unsaved);
    chunks.groups = static_cast<quint32>(numbers.groups);
    chunks.largestGroup = static_cast<quint32>(numbers.largestGroup);
    chunks.oversizeGroups = static_cast<quint32>(numbers.oversizeGroups);
    chunks.tickets = static_cast<quint32>(numbers.tickets);
    chunks.refusedTickets = static_cast<quint32>(std::min<quint64>(numbers.refusedTickets, std::numeric_limits<quint32>::max()));
    chunks.forced = static_cast<quint32>(numbers.forced);
    chunks.loaders = static_cast<quint32>(numbers.loaders);
    const QcChunkPos here = QcPhysics::blockContaining(client.motion.position).chunk();
    chunks.playerGroup = m_state.world().chunk(here) ? static_cast<quint32>(m_loading.groupOf(here).chunks.size()) : 0;
    return chunks;
}

QList<QcMsg::MobGoal> QcServerDebug::goalsNear(const QcServerClient &client) const
{
    using Kind = QcMsg::MobGoal::Kind;
    QList<QcMsg::MobGoal> goals;
    for (const QcCreature &mob : m_state.entities().creatures().mobs()) {
        if (mob.dormant || (mob.motion.position - client.motion.position).length() > goalRange) {
            continue;
        }
        if (goals.size() >= static_cast<qsizetype>(QcMsg::ServerDebug::maxGoals)) {
            return goals;
        }

        const auto chased = std::ranges::find_if(m_state.clients(), [&mob](const auto &other) {
            return mob.chasing != 0 && other->id == mob.chasing;
        });
        if (chased != m_state.clients().end()) {
            goals.append({mob.id, Kind::Chase, (*chased)->motion.position});
        } else if (mob.path.hasGoal()) {
            const Kind kind = mob.path.purpose == QcMobPath::Purpose::Flee ? Kind::Flee : Kind::Wander;
            goals.append({mob.id, kind, mob.path.goal});
        } else if (m_state.content().mobs()[mob.type].flying && mob.wanderDirection.length() > 0.0) {
            // Flyers drift without a goal: where they head shortly.
            goals.append({mob.id, Kind::Wander, mob.motion.position + mob.wanderDirection.normalized() * goalLookAhead});
        }
    }
    return goals;
}
