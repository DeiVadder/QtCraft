#include "clientcore/control/QcControlSessionCommands.h"

#include "clientcore/control/QcControlCommandTable.h"
#include "clientcore/control/QcControlContext.h"
#include "clientcore/control/QcControlReports.h"
#include "clientcore/debug/QcDebugJson.h"
#include "clientcore/session/QcClientSession.h"
#include "common/content/registry/QcContentRegistry.h"

#include <QJsonArray>

#include <optional>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

// Overwrites the fields the request names: content names as strings, colours as "#rrggbb".
[[nodiscard]] std::optional<QString> readAppearance(const QJsonObject &request, QcAppearance &look)
{
    for (const auto &[key, value] : {std::pair{u"outfit"_s, &look.outfit}, std::pair{u"hair"_s, &look.hair}, std::pair{u"cape"_s, &look.cape}}) {
        if (request.contains(key)) {
            if (!request.value(key).isString() || request.value(key).toString().size() > QcAppearance::maxNameLength) {
                return u"'%1' must be a content name"_s.arg(key);
            }
            *value = request.value(key).toString();
        }
    }

    for (const auto &[key, value] : {std::pair{u"skinColor"_s, &look.skinColor}, std::pair{u"hairColor"_s, &look.hairColor},
                                     std::pair{u"shirtColor"_s, &look.shirtColor}, std::pair{u"pantsColor"_s, &look.pantsColor}}) {
        if (request.contains(key)) {
            const QString text = request.value(key).toString();
            bool ok = false;
            const uint rgb = text.startsWith(u'#') && text.size() == 7 ? QStringView(text).mid(1).toUInt(&ok, 16) : 0;
            if (!ok) {
                return u"'%1' must be a colour like \"#3a7bc8\""_s.arg(key);
            }
            *value = rgb;
        }
    }
    return std::nullopt;
}

} // namespace

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
    table.add(u"chatLog"_s, [this](const QJsonObject &request) { chatLog(request); });
    table.add(u"players"_s, [this](const QJsonObject &request) { players(request); });
    table.add(u"appearance"_s, [this](const QJsonObject &request) { appearance(request); });
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

void QcControlSessionCommands::chatLog(const QJsonObject &request)
{
    m_context.finish(request, {{u"lines"_s, QJsonArray::fromStringList(m_context.chatHistory())}});
    if (request.value(u"clear"_s).toBool(false)) {
        m_context.clearChatHistory();
    }
}

void QcControlSessionCommands::players(const QJsonObject &request)
{
    QJsonArray list;
    for (const QcMsg::PlayerList::Entry &player : session().players()) {
        list.append(QJsonObject{{u"name"_s, player.name}, {u"entity"_s, qint64(player.entityId)}, {u"operator"_s, player.operatorRights}});
    }
    m_context.finish(request, {{u"players"_s, list}});
}

void QcControlSessionCommands::appearance(const QJsonObject &request)
{
    QcAppearance look = session().appearance();
    if (const std::optional<QString> error = readAppearance(request, look)) {
        m_context.fail(request, *error);
        return;
    }

    // The server checks the look and shows it to the others with a new spawn of this player.
    session().setAppearance(look);
    m_context.finish(request, QcControlReports::appearanceJson(look));
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
        session().debug().setFlags(0);
        m_context.finish(request, {});
        return;
    }

    const quint8 wanted = QcMsg::DebugSubscribe::stats | (request.value(u"goals"_s).toBool(false) ? QcMsg::DebugSubscribe::mobGoals : 0);
    const quint8 before = session().debug().flags();
    const std::optional<QcMsg::ServerDebug> &latest = session().debug().mirror().latest();
    const auto result = [this] { return QcDebugJson::toJson(QcDebugSnapshot::collect(session())); };
    if ((before & wanted) == wanted && latest) {
        m_context.finish(request, result());
        return;
    }

    // A new subscription answers once the server's next message arrived (at most half a second).
    const quint64 seenTick = latest ? latest->tick : 0;
    session().debug().setFlags(before | wanted);
    m_context.begin({request, QcControlActiveCommand::timeoutPolls(20),
                     [this, seenTick] {
                         const std::optional<QcMsg::ServerDebug> &arrived = session().debug().mirror().latest();
                         return arrived && arrived->tick > seenTick;
                     },
                     result});
}
