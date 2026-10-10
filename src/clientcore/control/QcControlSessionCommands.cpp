#include "QcControlSessionCommands.h"

#include "QcClientSession.h"
#include "QcContentRegistry.h"
#include "QcControlCommandTable.h"
#include "QcControlContext.h"
#include "QcDebugJson.h"

#include <QJsonArray>

using namespace Qt::StringLiterals;

QcControlSessionCommands::QcControlSessionCommands(QcControlContext &context)
    : m_context(context)
{
}

void QcControlSessionCommands::registerCommands(QcControlCommandTable &table)
{
    table.add(u"status"_s, [this](const QJsonObject &request) { status(request); });
    table.add(u"view"_s, [this](const QJsonObject &request) { view(request); });
    table.add(u"respawn"_s, [this](const QJsonObject &request) { respawn(request); });
    table.add(u"chat"_s, [this](const QJsonObject &request) { chat(request); });
    table.add(u"sounds"_s, [this](const QJsonObject &request) { sounds(request); });
    table.add(u"mods"_s, [this](const QJsonObject &request) { mods(request); });
    table.add(u"debug"_s, [this](const QJsonObject &request) { debug(request); });
}

QcClientSession &QcControlSessionCommands::session() const
{
    return *m_context.session();
}

void QcControlSessionCommands::status(const QJsonObject &request)
{
    m_context.finish(request, m_context.reports().playerStatus());
}

void QcControlSessionCommands::view(const QJsonObject &request)
{
    // Without a mode it only reports the current one.
    if (request.contains(u"mode"_s)) {
        const std::optional<QcCameraMode> mode = QcViewCamera::fromName(request.value(u"mode"_s).toString());
        if (!mode) {
            m_context.fail(request, u"'mode' must be first, behind, front or side"_s);
            return;
        }

        session().setCameraMode(*mode);
    }

    m_context.finish(request, {{u"mode"_s, QString::fromLatin1(QcViewCamera::name(session().cameraMode()))}});
}

void QcControlSessionCommands::respawn(const QJsonObject &request)
{
    session().respawn();
    m_context.begin({request, QcControlActiveCommand::timeoutPolls(10), [this] { return !session().isDead(); }});
}

void QcControlSessionCommands::chat(const QJsonObject &request)
{
    const quint64 startTick = session().tickCount();
    m_context.clearChatLog();
    session().sendChat(request.value(u"text"_s).toString());
    m_context.waitTicks(request, startTick, 3);
}

void QcControlSessionCommands::sounds(const QJsonObject &request)
{
    m_context.finish(request, m_context.reports().soundsJson());
    if (request.value(u"clear"_s).toBool(false)) {
        session().clearRecentSounds();
    }
}

void QcControlSessionCommands::mods(const QJsonObject &request)
{
    // Content packs the server loaded, base game first.
    QJsonArray packs;
    if (const auto content = session().content()) {
        for (const QcPackReference &pack : content->packs()) {
            packs.append(QJsonObject{{u"id"_s, pack.id}, {u"name"_s, pack.name}, {u"version"_s, pack.version}});
        }
    }

    m_context.finish(request, {{u"packs"_s, packs}});
}

void QcControlSessionCommands::debug(const QJsonObject &request)
{
    if (request.value(u"off"_s).toBool(false)) {
        session().setDebugSubscription(0);
        m_context.finish(request, {});
        return;
    }

    const quint8 wanted = QcMsg::DebugSubscribe::stats | (request.value(u"goals"_s).toBool(false) ? QcMsg::DebugSubscribe::mobGoals : 0);
    const quint8 before = session().debugSubscription();
    const std::optional<QcMsg::ServerDebug> &latest = session().serverDebug().latest();
    const auto result = [this] { return QcDebugJson::toJson(QcDebugSnapshot::collect(session())); };
    if ((before & wanted) == wanted && latest) {
        m_context.finish(request, result());
        return;
    }

    // A new subscription answers once the server's next message arrived (at most half a second).
    const quint64 seenTick = latest ? latest->tick : 0;
    session().setDebugSubscription(before | wanted);
    m_context.begin({request, QcControlActiveCommand::timeoutPolls(20),
                     [this, seenTick] {
                         const std::optional<QcMsg::ServerDebug> &arrived = session().serverDebug().latest();
                         return arrived && arrived->tick > seenTick;
                     },
                     result});
}
