#include "QcControlMovementCommands.h"

#include "QcClientSession.h"
#include "QcControlAim.h"
#include "QcControlArguments.h"
#include "QcControlCommandTable.h"
#include "QcControlContext.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr int defaultReadyTimeoutPolls = 20 * 60;

} // namespace

QcControlMovementCommands::QcControlMovementCommands(QcControlContext &context)
    : m_context(context)
{
}

void QcControlMovementCommands::registerCommands(QcControlCommandTable &table)
{
    table.add(u"look"_s, [this](const QJsonObject &request) { look(request); });
    table.add(u"move"_s, [this](const QJsonObject &request) { move(request, false); });
    table.add(u"jump"_s, [this](const QJsonObject &request) { move(request, true); });
    table.add(u"lookAt"_s, [this](const QJsonObject &request) { lookAt(request); });
    table.add(u"wait"_s, [this](const QJsonObject &request) { wait(request); });
    table.add(u"waitUntilReady"_s, [this](const QJsonObject &request) { waitUntilReady(request); });
}

QcClientSession &QcControlMovementCommands::session() const
{
    return *m_context.session();
}

void QcControlMovementCommands::look(const QJsonObject &request)
{
    QcPlayerInput intent = session().intent();
    // Wrapped in double precision first: huge values would overflow a float.
    intent.yaw = float(std::fmod(request.value(u"yaw"_s).toDouble(intent.yaw), 360.0));
    intent.pitch = float(std::clamp(request.value(u"pitch"_s).toDouble(intent.pitch), -90.0, 90.0));
    session().setIntent(intent);

    m_context.finish(request, m_context.reports().playerStatus());
}

void QcControlMovementCommands::lookAt(const QJsonObject &request)
{
    QcControlAim::lookAt(session(), QcControlArguments::block(request), std::nullopt);
    m_context.finish(request, m_context.reports().playerStatus());
}

void QcControlMovementCommands::move(const QJsonObject &request, bool isJump)
{
    const quint64 startTick = session().tickCount();

    QcPlayerInput intent = session().intent();
    intent.forward = float(std::clamp(request.value(u"forward"_s).toDouble(0.0), -1.0, 1.0));
    intent.strafe = float(std::clamp(request.value(u"strafe"_s).toDouble(0.0), -1.0, 1.0));
    intent.jump = isJump || request.value(u"jump"_s).toBool(false);
    intent.sneak = request.value(u"sneak"_s).toBool(false);
    intent.sprint = request.value(u"sprint"_s).toBool(false);
    session().setIntent(intent);

    const int ticks = QcControlArguments::ticks(request, isJump ? 2 : 20);
    m_context.begin({request, QcControlActiveCommand::timeoutPolls(ticks), [this, startTick, ticks] {
                         if (session().tickCount() < startTick + quint64(ticks)) {
                             return false;
                         }

                         QcPlayerInput stop = session().intent();
                         stop.forward = 0.0f;
                         stop.strafe = 0.0f;
                         stop.jump = false;
                         stop.sneak = false;
                         stop.sprint = false;
                         session().setIntent(stop);

                         return true;
                     }});
}

void QcControlMovementCommands::wait(const QJsonObject &request)
{
    m_context.waitTicks(request, session().tickCount(), QcControlArguments::ticks(request, 20));
}

void QcControlMovementCommands::waitUntilReady(const QJsonObject &request)
{
    const int timeoutPolls = std::clamp(QcControlArguments::intField(request, u"timeoutPolls"_s, defaultReadyTimeoutPolls), 1,
                                        QcControlArguments::maxCommandTicks);
    m_context.begin({request, timeoutPolls, [this] { return isReady(); }});
}

bool QcControlMovementCommands::isReady() const
{
    const QcClientWorld *world = session().world();
    if (session().state() != QcClientSession::State::Playing || !world) {
        return false;
    }

    const QcVec3 p = session().playerMotion().position;
    const QcChunkPos centre = QcBlockPos{int(std::floor(p.x)), 0, int(std::floor(p.z))}.chunk();

    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            if (!world->chunk({centre.x + dx, centre.z + dz})) {
                return false;
            }
        }
    }

    // Settled: the player stands on something or floats in water.
    return session().playerMotion().onGround || session().playerMotion().inWater;
}
