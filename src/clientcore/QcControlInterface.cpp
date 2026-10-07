#include "QcControlInterface.h"

#include "QcClientSession.h"
#include "QcContentRegistry.h"

#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaEnum>
#include <QTimer>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr int pollIntervalMs = 50;
constexpr int maxBlockQueryRadius = 8;
constexpr int maxCommandTicks = 20 * 600;
constexpr int defaultReadyTimeoutPolls = 20 * 60;

// Tick-based commands fail when the session stops ticking (e.g. while loading) for much longer than asked.
int timeoutPolls(int ticks)
{
    return ticks * 4 + 100;
}

QJsonArray vectorJson(const QcVec3 &v)
{
    return {v.x, v.y, v.z};
}

int intField(const QJsonObject &request, const QString &key, int fallback)
{
    return request.value(key).toInt(fallback);
}

} // namespace

QcControlInterface::QcControlInterface(QcClientSession *session, QIODevice *input, QIODevice *output, QObject *parent)
    : QObject(parent)
    , m_session(session)
    , m_input(input)
    , m_output(output)
{
    if (m_input) {
        connect(m_input, &QIODevice::readyRead, this, &QcControlInterface::readInput);
    }
    auto *poll = new QTimer(this);
    poll->setInterval(pollIntervalMs);
    connect(poll, &QTimer::timeout, this, &QcControlInterface::onTick);
    poll->start();
}

void QcControlInterface::readInput()
{
    feed(m_input->readAll());
}

void QcControlInterface::feed(const QByteArray &data)
{
    m_lineBuffer.append(data);
    qsizetype newline = m_lineBuffer.indexOf('\n');
    while (newline >= 0) {
        const QByteArray line = m_lineBuffer.left(newline).trimmed();
        m_lineBuffer.remove(0, newline + 1);
        newline = m_lineBuffer.indexOf('\n');
        ++m_lineNumber;
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }
        QJsonParseError error{};
        const QJsonDocument document = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            // Queued like a request so the error is answered in input order.
            const QString reason = error.error != QJsonParseError::NoError ? error.errorString() : u"not an object"_s;
            submit({{QLatin1StringView(parseErrorKey), u"Invalid JSON request in line %1: %2"_s.arg(m_lineNumber).arg(reason)}});
            continue;
        }
        submit(document.object());
    }
}

void QcControlInterface::submit(const QJsonObject &request)
{
    m_queue.enqueue(request);
    processQueue();
}

void QcControlInterface::processQueue()
{
    while (!m_active && !m_queue.isEmpty()) {
        start(m_queue.dequeue());
    }
    if (isIdle()) {
        emit idle();
    }
}

void QcControlInterface::finish(const QJsonObject &request, QJsonObject response)
{
    if (request.contains(u"id"_s)) {
        response.insert(u"id"_s, request.value(u"id"_s));
    }
    response.insert(u"ok"_s, true);
    if (m_output) {
        m_output->write(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n');
    }
}

void QcControlInterface::fail(const QJsonObject &request, const QString &message)
{
    ++m_failedRequests;
    QJsonObject response{{u"ok"_s, false}, {u"error"_s, message}};
    if (request.contains(u"id"_s)) {
        response.insert(u"id"_s, request.value(u"id"_s));
    }
    if (request.contains(u"cmd"_s)) {
        response.insert(u"cmd"_s, request.value(u"cmd"_s));
    }
    if (m_output) {
        m_output->write(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n');
    }
}

QJsonObject QcControlInterface::playerStatus() const
{
    QJsonObject status;
    const QMetaEnum stateEnum = QMetaEnum::fromType<QcClientSession::State>();
    status.insert(u"state"_s, QString::fromLatin1(stateEnum.valueToKey(int(m_session->state()))));
    status.insert(u"tick"_s, qint64(m_session->tickCount()));
    status.insert(u"world"_s, m_session->worldName());
    status.insert(u"seed"_s, QString::number(m_session->seed()));
    const QcEntityMotion &motion = m_session->playerMotion();
    status.insert(u"position"_s, vectorJson(motion.position));
    status.insert(u"velocity"_s, vectorJson(motion.velocity));
    status.insert(u"onGround"_s, motion.onGround);
    status.insert(u"inWater"_s, motion.inWater);
    status.insert(u"yaw"_s, m_session->intent().yaw);
    status.insert(u"pitch"_s, m_session->intent().pitch);
    status.insert(u"loadedChunks"_s, m_session->world() ? qint64(m_session->world()->chunkCount()) : 0);
    if (m_session->state() == QcClientSession::State::Disconnected) {
        status.insert(u"disconnectReason"_s, m_session->disconnectReason());
    }
    return status;
}

void QcControlInterface::start(const QJsonObject &request)
{
    if (!m_session) {
        fail(request, u"No session"_s);
        return;
    }
    if (request.contains(QLatin1StringView(parseErrorKey))) {
        fail({}, request.value(QLatin1StringView(parseErrorKey)).toString());
        return;
    }
    for (const char *key : {"yaw", "pitch", "forward", "strafe", "ticks", "radius", "x", "y", "z", "timeoutPolls"}) {
        const QJsonValue value = request.value(QLatin1StringView(key));
        if (!value.isUndefined() && !value.isDouble()) {
            fail(request, u"'%1' must be a number"_s.arg(QLatin1StringView(key)));
            return;
        }
    }
    for (const char *key : {"jump", "sneak", "sprint"}) {
        const QJsonValue value = request.value(QLatin1StringView(key));
        if (!value.isUndefined() && !value.isBool()) {
            fail(request, u"'%1' must be true or false"_s.arg(QLatin1StringView(key)));
            return;
        }
    }
    const QString command = request.value(u"cmd"_s).toString();
    const quint64 startTick = m_session->tickCount();
    const auto ticksArgument = [&](int fallback) { return std::clamp(intField(request, u"ticks"_s, fallback), 0, maxCommandTicks); };

    if (command == u"status"_s) {
        finish(request, playerStatus());
    } else if (command == u"look"_s) {
        QcPlayerInput intent = m_session->intent();
        // Wrapped in double precision first: huge values would overflow a float.
        intent.yaw = float(std::fmod(request.value(u"yaw"_s).toDouble(intent.yaw), 360.0));
        intent.pitch = float(std::clamp(request.value(u"pitch"_s).toDouble(intent.pitch), -90.0, 90.0));
        m_session->setIntent(intent);
        finish(request, playerStatus());
    } else if (command == u"move"_s || command == u"jump"_s) {
        const bool isJump = command == u"jump"_s;
        QcPlayerInput intent = m_session->intent();
        intent.forward = float(std::clamp(request.value(u"forward"_s).toDouble(0.0), -1.0, 1.0));
        intent.strafe = float(std::clamp(request.value(u"strafe"_s).toDouble(0.0), -1.0, 1.0));
        intent.jump = isJump || request.value(u"jump"_s).toBool(false);
        intent.sneak = request.value(u"sneak"_s).toBool(false);
        intent.sprint = request.value(u"sprint"_s).toBool(false);
        m_session->setIntent(intent);
        const int ticks = ticksArgument(isJump ? 2 : 20);
        m_active = ActiveCommand{request, timeoutPolls(ticks), [this, startTick, ticks] {
                                     if (m_session->tickCount() < startTick + quint64(ticks)) {
                                         return false;
                                     }
                                     QcPlayerInput stop = m_session->intent();
                                     stop.forward = 0.0f;
                                     stop.strafe = 0.0f;
                                     stop.jump = false;
                                     stop.sneak = false;
                                     stop.sprint = false;
                                     m_session->setIntent(stop);
                                     return true;
                                 }};
    } else if (command == u"wait"_s) {
        const int ticks = ticksArgument(20);
        m_active = ActiveCommand{request, timeoutPolls(ticks), [this, startTick, ticks] {
                                     return m_session->tickCount() >= startTick + quint64(ticks);
                                 }};
    } else if (command == u"waitUntilReady"_s) {
        const int timeoutPolls = std::clamp(intField(request, u"timeoutPolls"_s, defaultReadyTimeoutPolls), 1, maxCommandTicks);
        m_active = ActiveCommand{request, timeoutPolls, [this] {
                                     const QcClientWorld *world = m_session->world();
                                     if (m_session->state() != QcClientSession::State::Playing || !world) {
                                         return false;
                                     }
                                     const QcVec3 p = m_session->playerMotion().position;
                                     const QcChunkPos centre =
                                         QcBlockPos{int(std::floor(p.x)), 0, int(std::floor(p.z))}.chunk();
                                     for (int dx = -1; dx <= 1; ++dx) {
                                         for (int dz = -1; dz <= 1; ++dz) {
                                             if (!world->chunk({centre.x + dx, centre.z + dz})) {
                                                 return false;
                                             }
                                         }
                                     }
                                     // Settled: the player stands on something or floats in water.
                                     return m_session->playerMotion().onGround || m_session->playerMotion().inWater;
                                 }};
    } else if (command == u"block"_s || command == u"blocks"_s) {
        const QcClientWorld *world = m_session->world();
        if (!world) {
            fail(request, u"World not loaded yet"_s);
            return;
        }
        const QcContentRegistry &content = world->content();
        const auto describe = [&](QcBlockPos pos) -> QJsonValue {
            const std::optional<QcBlockId> id = world->blockAt(pos);
            return id ? QJsonValue(content.block(*id).name) : QJsonValue();
        };
        if (command == u"block"_s) {
            const QcBlockPos pos{intField(request, u"x"_s, 0), intField(request, u"y"_s, 0), intField(request, u"z"_s, 0)};
            finish(request, {{u"block"_s, describe(pos)}});
            return;
        }
        const int radius = std::clamp(intField(request, u"radius"_s, 3), 0, maxBlockQueryRadius);
        const QcVec3 p = m_session->playerMotion().position;
        const QcBlockPos centre{int(std::floor(p.x)), int(std::floor(p.y)), int(std::floor(p.z))};
        QJsonArray blocks;
        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dz = -radius; dz <= radius; ++dz) {
                    const QcBlockPos pos = centre.offset(dx, dy, dz);
                    const std::optional<QcBlockId> id = world->blockAt(pos);
                    if (id && *id != qcAirBlockId) {
                        blocks.append(QJsonObject{{u"x"_s, pos.x}, {u"y"_s, pos.y}, {u"z"_s, pos.z},
                                                  {u"block"_s, content.block(*id).name}});
                    }
                }
            }
        }
        finish(request, {{u"centre"_s, QJsonArray{centre.x, centre.y, centre.z}}, {u"blocks"_s, blocks}});
    } else if (command == u"quit"_s) {
        finish(request, {});
        emit quitRequested();
    } else {
        QJsonObject response;
        if (m_extension && m_extension(request, response)) {
            if (response.value(u"ok"_s).toBool(true)) {
                finish(request, response);
            } else {
                fail(request, response.value(u"error"_s).toString());
            }
            return;
        }
        fail(request, u"Unknown command '%1'"_s.arg(command));
    }
}

void QcControlInterface::onTick()
{
    if (!m_active) {
        return;
    }
    if (!m_session) {
        const QJsonObject request = m_active->request;
        m_active.reset();
        fail(request, u"Session ended"_s);
        processQueue();
        return;
    }
    if (m_session->state() == QcClientSession::State::Disconnected) {
        const QJsonObject request = m_active->request;
        m_active.reset();
        fail(request, u"Disconnected: %1"_s.arg(m_session->disconnectReason()));
        processQueue();
        return;
    }
    if (m_active->step()) {
        const QJsonObject request = m_active->request;
        m_active.reset();
        finish(request, playerStatus());
        processQueue();
        return;
    }
    if (--m_active->remainingTicks <= 0) {
        const QJsonObject request = m_active->request;
        m_active.reset();
        fail(request, u"Timed out"_s);
        processQueue();
    }
}
