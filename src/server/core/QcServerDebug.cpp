#include "QcServerDebug.h"

#include "QcBlockUpdateQueue.h"
#include "QcChunkLoading.h"
#include "QcServerClient.h"
#include "QcServerState.h"
#include "QcTickPhaseTimer.h"
#include "QcTickScheduler.h"

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
        message.phaseMeanMs[std::size_t(phase)] = float(window.mean(phase));
        message.phaseMaxMs[std::size_t(phase)] = float(window.max(phase));
    }
    message.tickMeanMs = float(window.mean(QcTickPhaseTimer::durationSeries));
    message.tickMaxMs = float(window.max(QcTickPhaseTimer::durationSeries));
    message.lateMeanMs = float(window.mean(QcTickPhaseTimer::lateSeries));
    message.lateMaxMs = float(window.max(QcTickPhaseTimer::lateSeries));
    const quint64 since = client.view.debugSentTick ? m_state.clock().tick() - *client.view.debugSentTick : intervalTicks;
    message.recentTickMs = window.history(QcTickPhaseTimer::durationSeries, int(std::min<quint64>(since, QcMsg::ServerDebug::maxRecentTicks)));

    message.tick = m_state.clock().tick();
    message.skippedTicks = m_ticks.stats().skipped();
    message.loadedChunks = quint32(m_state.world().loadedChunkCount());
    message.players = quint32(std::ranges::count_if(m_state.clients(), [](const auto &other) { return other->joined; }));
    message.mobs = quint32(m_state.entities().creatures().awakeCount());
    message.items = quint32(m_state.entities().items().items().size());
    message.fallingBlocks = quint32(m_state.entities().fallingBlocks().all().size());
    message.pendingBlockUpdates = quint32(m_updates.pendingPlayerCount() + m_updates.pendingCommandCount());
    message.waitingBlockUpdates = quint32(m_updates.waitingCount());
    const QcVec3 &feet = client.motion.position;
    message.biome = m_state.world().generator().biomeAt(int(std::floor(feet.x)), int(std::floor(feet.z))).left(QcMsg::ServerDebug::maxTextLength);
    message.worldType = m_state.config().generator.left(QcMsg::ServerDebug::maxTextLength);

    addChunks(client, message);
    if ((client.view.debugFlags & QcMsg::DebugSubscribe::mobGoals) != 0) {
        addGoals(client, message);
    }
    return message;
}

void QcServerDebug::addChunks(const QcServerClient &client, QcMsg::ServerDebug &message) const
{
    const QcChunkLoading::Numbers numbers = m_loading.numbers();
    QcMsg::ServerDebug::Chunks &chunks = message.chunks;
    chunks.stored = quint32(numbers.stored);
    chunks.arriving = quint32(numbers.waiting);
    chunks.unsaved = quint32(numbers.unsaved);
    chunks.groups = quint32(numbers.groups);
    chunks.largestGroup = quint32(numbers.largestGroup);
    chunks.oversizeGroups = quint32(numbers.oversizeGroups);
    chunks.tickets = quint32(numbers.tickets);
    chunks.refusedTickets = quint32(std::min<quint64>(numbers.refusedTickets, std::numeric_limits<quint32>::max()));
    chunks.forced = quint32(numbers.forced);
    chunks.loaders = quint32(numbers.loaders);
    const QcChunkPos here = QcPhysics::blockContaining(client.motion.position).chunk();
    chunks.playerGroup = m_state.world().chunk(here) ? quint32(m_loading.groupOf(here).chunks.size()) : 0;
}

void QcServerDebug::addGoals(const QcServerClient &client, QcMsg::ServerDebug &message) const
{
    using Kind = QcMsg::MobGoal::Kind;
    for (const QcCreature &mob : m_state.entities().creatures().mobs()) {
        if (mob.dormant || (mob.motion.position - client.motion.position).length() > goalRange) {
            continue;
        }
        if (message.goals.size() >= qsizetype(QcMsg::ServerDebug::maxGoals)) {
            return;
        }

        const auto chased = std::ranges::find_if(m_state.clients(), [&mob](const auto &other) {
            return mob.chasing != 0 && other->id == mob.chasing;
        });
        if (chased != m_state.clients().end()) {
            message.goals.append({mob.id, Kind::Chase, (*chased)->motion.position});
        } else if (mob.path.hasGoal()) {
            const Kind kind = mob.path.purpose == QcMobPath::Purpose::Flee ? Kind::Flee : Kind::Wander;
            message.goals.append({mob.id, kind, mob.path.goal});
        } else if (m_state.content().mobs()[mob.type].flying && mob.wanderDirection.length() > 0.0) {
            // Flyers drift without a goal: where they head shortly.
            message.goals.append({mob.id, Kind::Wander, mob.motion.position + mob.wanderDirection.normalized() * goalLookAhead});
        }
    }
}
