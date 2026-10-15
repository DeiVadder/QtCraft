// Payloads of the debug messages (QcDebugMessages.h): the subscription and the server's statistics.

#include "QcProtocolCodec.h"

#include "QcStreamLimits.h"
#include "QcWorldConstants.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// Times are finite and not negative; a day of ticks is the most any of them can sensibly be.
bool validTime(float ms)
{
    return std::isfinite(ms) && ms >= 0.0f && ms <= 86'400'000.0f;
}

bool validTimes(const QcMsg::ServerDebug &m)
{
    for (std::size_t phase = 0; phase < m.phaseMeanMs.size(); ++phase) {
        if (!validTime(m.phaseMeanMs[phase]) || !validTime(m.phaseMaxMs[phase])) {
            return false;
        }
    }
    for (const float value : {m.tickMeanMs, m.tickMaxMs, m.lateMeanMs, m.lateMaxMs}) {
        if (!validTime(value)) {
            return false;
        }
    }
    return true;
}

bool readRecentTicks(QDataStream &stream, QList<float> &ticks)
{
    quint32 count = 0;
    if (!QcStreamLimits::readCount(stream, QcMsg::ServerDebug::maxRecentTicks, count)) {
        return false;
    }
    for (quint32 i = 0; i < count; ++i) {
        float ms = 0.0f;
        stream >> ms;
        if (!validTime(ms)) {
            return false;
        }
        ticks.append(ms);
    }
    return true;
}

// A goal is only drawn: one far outside the world moves to its edge instead of reaching the
// line builder with coordinates no float can place sensibly.
QcVec3 clampedToWorld(const QcVec3 &goal)
{
    constexpr double horizontal = QcWorld::maxHorizontalBlock;
    constexpr double vertical = QcWorld::worldHeight;
    return {std::clamp(goal.x, -horizontal, horizontal), std::clamp(goal.y, -vertical, 2.0 * vertical), std::clamp(goal.z, -horizontal, horizontal)};
}

bool readGoals(QDataStream &stream, QList<QcMsg::MobGoal> &goals)
{
    using QcProtocolCodec::operator>>;
    quint32 count = 0;
    if (!QcStreamLimits::readCount(stream, QcMsg::ServerDebug::maxGoals, count)) {
        return false;
    }
    for (quint32 i = 0; i < count; ++i) {
        QcMsg::MobGoal goal;
        quint8 kind = 0;
        stream >> goal.entityId >> kind >> goal.goal;
        if (kind >= QcMsg::MobGoal::kindCount || !QcProtocolCodec::isFinite(goal.goal)) {
            return false;
        }
        goal.kind = QcMsg::MobGoal::Kind(kind);
        goal.goal = clampedToWorld(goal.goal);
        goals.append(goal);
    }
    return true;
}

} // namespace

namespace QcProtocolCodec {

void encode(QDataStream &stream, QcMsg::DebugSubscribe m)
{
    stream << m.flags;
}

void encode(QDataStream &stream, const QcMsg::ServerDebug &m)
{
    for (std::size_t phase = 0; phase < m.phaseMeanMs.size(); ++phase) {
        stream << m.phaseMeanMs[phase] << m.phaseMaxMs[phase];
    }
    stream << m.tickMeanMs << m.tickMaxMs << m.lateMeanMs << m.lateMaxMs;

    stream << quint32(m.recentTickMs.size());
    for (const float ms : m.recentTickMs) {
        stream << ms;
    }

    stream << m.tick << m.skippedTicks << m.loadedChunks << m.players << m.mobs << m.items << m.fallingBlocks << m.pendingBlockUpdates
           << m.waitingBlockUpdates << m.biome << m.worldType;
    const QcMsg::ServerDebug::Chunks &c = m.chunks;
    stream << c.stored << c.arriving << c.unsaved << c.groups << c.largestGroup << c.playerGroup << c.oversizeGroups << c.tickets << c.refusedTickets
           << c.forced << c.loaders;

    stream << quint32(m.goals.size());
    for (const QcMsg::MobGoal &goal : m.goals) {
        stream << goal.entityId << quint8(goal.kind) << goal.goal;
    }
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::DebugSubscribe>, QDataStream &stream)
{
    QcMsg::DebugSubscribe m;
    stream >> m.flags;
    if ((m.flags & ~QcMsg::DebugSubscribe::all) != 0) {
        return qcError(u"Unknown debug data requested"_s);
    }
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ServerDebug>, QDataStream &stream)
{
    QcMsg::ServerDebug m;
    for (std::size_t phase = 0; phase < m.phaseMeanMs.size(); ++phase) {
        stream >> m.phaseMeanMs[phase] >> m.phaseMaxMs[phase];
    }
    stream >> m.tickMeanMs >> m.tickMaxMs >> m.lateMeanMs >> m.lateMaxMs;
    if (!validTimes(m) || !readRecentTicks(stream, m.recentTickMs)) {
        return qcError(u"Invalid tick times"_s);
    }

    stream >> m.tick >> m.skippedTicks >> m.loadedChunks >> m.players >> m.mobs >> m.items >> m.fallingBlocks >> m.pendingBlockUpdates
        >> m.waitingBlockUpdates >> m.biome >> m.worldType;
    QcMsg::ServerDebug::Chunks &c = m.chunks;
    stream >> c.stored >> c.arriving >> c.unsaved >> c.groups >> c.largestGroup >> c.playerGroup >> c.oversizeGroups >> c.tickets >> c.refusedTickets
        >> c.forced >> c.loaders;
    if (m.biome.size() > QcMsg::ServerDebug::maxTextLength || m.worldType.size() > QcMsg::ServerDebug::maxTextLength) {
        return qcError(u"Invalid server debug text"_s);
    }

    if (!readGoals(stream, m.goals)) {
        return qcError(u"Invalid creature goals"_s);
    }
    return m;
}

} // namespace QcProtocolCodec
