// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/admin/QcServerAdmin.h"

#include "common/core/QcProcessMemory.h"
#include "common/diagnostics/QcMessageLog.h"
#include "common/protocol/QcChatText.h"
#include "server/commands/QcAccessList.h"
#include "server/commands/QcServerCommands.h"
#include "server/commands/QcWorldBackup.h"
#include "server/core/QcServerLogging.h"
#include "server/core/QcServerState.h"

#include <QDateTime>
#include <QRegularExpression>

using namespace Qt::StringLiterals;
using Kind = QcMsg::AdminRequest::Kind;
namespace Limits = QcMsg::AdminLimits;

namespace {

// The request's name in the log.
QLatin1StringView kindName(Kind kind)
{
    static constexpr std::array<const char *, QcMsg::AdminRequest::kindCount> names{
        "catalogue", "players", "lists",       "settings",         "log",     "command", "chat",    "kick", "ban",    "unban",
        "op",        "deop",    "whitelist add", "whitelist remove", "whitelist", "gamerule", "limit", "save", "backup", "stop"};
    return QLatin1StringView(names[static_cast<std::size_t>(kind)]);
}

// A word of a command line: not empty and without spaces, so it cannot shift the words after it.
bool isWord(const QString &text)
{
    return !text.isEmpty() && !text.contains(QRegularExpression(u"\\s"_s));
}

// The command of a request that names a player, and whether a reason may follow the name.
struct NamedCommand
{
    const char *command = nullptr;
    bool reason = false;
};

std::optional<NamedCommand> namedCommand(Kind kind)
{
    switch (kind) {
    case Kind::Kick:
        return NamedCommand{"kick", true};
    case Kind::Ban:
        return NamedCommand{"ban", true};
    case Kind::Unban:
        return NamedCommand{"unban", false};
    case Kind::Op:
        return NamedCommand{"op", false};
    case Kind::Deop:
        return NamedCommand{"deop", false};
    case Kind::WhitelistAdd:
        return NamedCommand{"whitelist add", false};
    case Kind::WhitelistRemove:
        return NamedCommand{"whitelist remove", false};
    default:
        return std::nullopt;
    }
}

// The command line of a game rule or limit request (name, value; "default" resets a limit).
QString settingLine(Kind kind, const QStringList &args)
{
    if (args.size() != 2 || !isWord(args[0]) || !isWord(args[1])) {
        return {};
    }
    if (kind == Kind::SetRule) {
        return u"/gamerule %1 %2"_s.arg(args[0], args[1]);
    }
    return args[1] == u"default"_s ? u"/limits reset "_s + args[0] : u"/limits set %1 %2"_s.arg(args[0], args[1]);
}

QcMsg::AdminResult refusal(quint32 id, QcMsg::AdminResult::Refusal reason, const char *key)
{
    return {id, false, {QcChatText::message(key)}, reason};
}

constexpr const char *rightsText = QT_TRANSLATE_NOOP("QcServerMessages", "Only operators can use the server administration");

// The screen offers the fields at the lengths the server takes.
static_assert(QcMsg::AdminLimits::maxReason == QcAccessList::maxReasonLength);
static_assert(QcMsg::AdminLimits::maxBackupLabel == QcWorldBackup::maxLabelLength);

} // namespace

QcServerAdmin::QcServerAdmin(QcServerState &state, QcServerCommands &commands, const QcTickPhaseTimer &phases, const QcTickScheduler &ticks)
    : m_state(state)
    , m_commands(commands)
    , m_reports(state, commands.registry(), phases, ticks)
{
    m_uptime.start();
}

bool QcServerAdmin::handle(QcServerClient &client, const QcMessage &message)
{
    if (const auto *subscription = std::get_if<QcMsg::AdminSubscribe>(&message)) {
        if (authorized(client, 0, QLatin1StringView("subscribe"))) {
            subscribe(client, *subscription);
        }
    } else if (const auto *asked = std::get_if<QcMsg::AdminRequest>(&message)) {
        if (authorized(client, asked->id, kindName(asked->kind))) {
            request(client, *asked);
        }
    } else if (const auto *answer = std::get_if<QcMsg::Pong>(&message)) {
        pong(client, answer->nonce);
    } else {
        return false;
    }
    return true;
}

bool QcServerAdmin::authorized(QcServerClient &client, quint32 id, QLatin1StringView what)
{
    if (m_state.permissionOf(client) != QcPermission::Player) {
        return true;
    }
    qCWarning(lcAdmin).noquote() << u"Refused admin %1 of %2: not an operator"_s.arg(what, client.name);
    // A subscription that ends here is ended for the client too (number 0), whatever was asked.
    if (m_subscribers.remove(client.id) && id != 0) {
        client.connection->send(refusal(0, QcMsg::AdminResult::Refusal::Rights, rightsText));
    }
    client.connection->send(refusal(id, QcMsg::AdminResult::Refusal::Rights, rightsText));
    // The refusal tells him he is no operator (tellRights).
    m_operators.remove(client.id);
    return false;
}

void QcServerAdmin::subscribe(QcServerClient &client, QcMsg::AdminSubscribe subscribe)
{
    const quint8 before = m_subscribers.value(client.id).topics;
    if (subscribe.topics == 0) {
        m_subscribers.remove(client.id);
        client.connection->send(QcMsg::AdminResult{0, true, {}});
        return;
    }

    Subscriber &subscriber = m_subscribers[client.id];
    const QcMsg::AdminLogLine::Level previousLevel = subscriber.logLevel;
    subscriber.topics = subscribe.topics;
    subscriber.logLevel = static_cast<QcMsg::AdminLogLine::Level>(subscribe.logLevel);
    const quint8 added = subscribe.topics & ~before;
    if ((added & QcMsg::AdminSubscribe::status) != 0) {
        client.connection->send(QcMsg::AdminHistory{m_recorder.seconds(), m_recorder.minutes()});
        if (const std::optional<QcMsg::AdminSample> latest = m_recorder.latest()) {
            client.connection->send(m_reports.status(*latest, static_cast<quint64>(m_uptime.elapsed())));
        }
        client.connection->send(m_reports.players(pings()));
    }
    if ((added & QcMsg::AdminSubscribe::chat) != 0) {
        client.connection->send(QcMsg::AdminChat{true, m_commands.chatLog().since(0)});
        subscriber.chatSeen = m_commands.chatLog().latest();
    }
    // The log followed anew or at another level starts from its tail at that level.
    const bool newLevel = (before & QcMsg::AdminSubscribe::log) != 0 && subscriber.logLevel != previousLevel;
    if ((added & QcMsg::AdminSubscribe::log) != 0 || ((subscribe.topics & QcMsg::AdminSubscribe::log) != 0 && newLevel)) {
        const QcAdminReports::Log tail = m_reports.log(subscriber.logLevel, tailLines, 0, true);
        subscriber.logSeen = tail.latest;
        client.connection->send(tail.message);
    }
    client.connection->send(QcMsg::AdminResult{0, true, {}});
}

void QcServerAdmin::request(QcServerClient &client, const QcMsg::AdminRequest &request)
{
    qCInfo(lcAdmin).noquote() << u"%1: admin %2 %3"_s.arg(client.name, kindName(request.kind), request.arguments.join(u' ').left(Limits::maxText));
    if (request.kind == Kind::LogTail) {
        logTail(client, request);
        return;
    }
    if (report(client, request)) {
        client.connection->send(QcMsg::AdminResult{request.id, true, {}});
        return;
    }

    if (request.kind == Kind::Stop) {
        if (request.arguments != QStringList{u"confirm"_s}) {
            client.connection->send(refusal(request.id, QcMsg::AdminResult::Refusal::Confirmation,
                                            QT_TRANSLATE_NOOP("QcServerMessages", "Stopping the server needs the confirmation")));
            return;
        }
        qCWarning(lcAdmin).noquote() << u"%1 stops the server through the server administration"_s.arg(client.name);
        run(client, request.id, u"/stop"_s, true);
        return;
    }

    const QString line = commandLine(request);
    if (line.isEmpty()) {
        qCWarning(lcAdmin).noquote() << u"Refused admin %1 of %2: wrong arguments"_s.arg(kindName(request.kind), client.name);
        client.connection->send(refusal(request.id, QcMsg::AdminResult::Refusal::Arguments,
                                        QT_TRANSLATE_NOOP("QcServerMessages", "The request has wrong arguments")));
        return;
    }
    run(client, request.id, line, false);
    sendChanged(client, request.kind);
}

bool QcServerAdmin::report(QcServerClient &client, const QcMsg::AdminRequest &request)
{
    switch (request.kind) {
    case Kind::Catalogue:
        client.connection->send(m_reports.catalogue());
        return true;
    case Kind::Players:
        client.connection->send(m_reports.players(pings()));
        return true;
    case Kind::Lists:
        client.connection->send(m_reports.lists());
        return true;
    case Kind::Settings:
        client.connection->send(m_reports.settings());
        return true;
    default:
        return false;
    }
}

QString QcServerAdmin::commandLine(const QcMsg::AdminRequest &request)
{
    const QStringList &args = request.arguments;
    if (const std::optional<NamedCommand> named = namedCommand(request.kind)) {
        // A name, and for kick and ban a reason that may have spaces.
        const bool fits = !args.isEmpty() && isWord(args[0]) && (args.size() == 1 || (named->reason && args.size() == 2));
        return fits ? QString(u"/%1 %2"_s.arg(QLatin1StringView(named->command), args.join(u' '))).trimmed() : QString();
    }
    if (request.kind == Kind::SetRule || request.kind == Kind::SetLimit) {
        return settingLine(request.kind, args);
    }

    const bool oneText = args.size() == 1 && !args[0].trimmed().isEmpty();
    switch (request.kind) {
    case Kind::Command:
        return oneText ? args[0].trimmed() : QString();
    case Kind::Chat:
        return oneText ? u"/say "_s + args[0].trimmed() : QString();
    case Kind::Whitelist:
        return args == QStringList{u"on"_s} || args == QStringList{u"off"_s} ? u"/whitelist "_s + args[0] : QString();
    case Kind::Save:
        return args.isEmpty() ? u"/save"_s : QString();
    case Kind::Backup:
        return args.isEmpty() || (args.size() == 1 && isWord(args[0])) ? QString(u"/backup "_s + args.value(0)).trimmed() : QString();
    default:
        return {};
    }
}

void QcServerAdmin::logTail(QcServerClient &client, const QcMsg::AdminRequest &request)
{
    // A tail is up to 2 000 lines of 1 024 characters: one a second at most.
    const qint64 now = m_state.clock().playerTimeNs();
    const auto last = m_lastTail.constFind(client.id);
    if (last != m_lastTail.cend() && now - *last < statusIntervalNs) {
        client.connection->send(refusal(request.id, QcMsg::AdminResult::Refusal::TooSoon,
                                        QT_TRANSLATE_NOOP("QcServerMessages", "Ask for the log at most once a second")));
        return;
    }
    m_lastTail.insert(client.id, now);

    const int level = request.arguments.value(0, u"1"_s).toInt();
    const qsizetype lines = request.arguments.size() > 1 ? request.arguments[1].toInt() : tailLines;
    const int boundedLevel = std::clamp(level, 0, static_cast<int>(QcMsg::AdminLogLine::levelCount) - 1);
    const qsizetype boundedLines = std::clamp<qsizetype>(lines, 0, Limits::maxLogLines);
    if (boundedLevel != level || boundedLines != lines) {
        qCWarning(lcAdmin).noquote() << u"Log tail of %1: level %2 and %3 lines bounded to %4 and %5"_s.arg(client.name).arg(level).arg(lines).arg(boundedLevel).arg(boundedLines);
    }
    const QcAdminReports::Log tail = m_reports.log(static_cast<QcMsg::AdminLogLine::Level>(boundedLevel), boundedLines, 0, true);
    // The tail replaces what the client shows: new lines follow from its newest on, not twice.
    if (const auto subscriber = m_subscribers.find(client.id); subscriber != m_subscribers.end()) {
        subscriber->logSeen = std::max(subscriber->logSeen, tail.latest);
    }
    client.connection->send(tail.message);
    client.connection->send(QcMsg::AdminResult{request.id, true, {}});
}

void QcServerAdmin::run(QcServerClient &client, quint32 id, const QString &line, bool console)
{
    QcMsg::AdminResult result{id, true, {}};
    const auto output = [&result](const QcMsg::ChatMessage &reply) {
        if (std::cmp_less(result.lines.size(), Limits::maxLines)) {
            result.lines.append(reply);
        }
    };
    if (console) {
        m_commands.console(line, output);
    } else {
        m_commands.admin(client, line, output);
    }
    // A command that took the rights away (deop of oneself) still answers this request.
    client.connection->send(result);
}

void QcServerAdmin::sendChanged(QcServerClient &client, Kind kind)
{
    if (kind == Kind::SetRule || kind == Kind::SetLimit) {
        client.connection->send(m_reports.settings());
    } else if (kind >= Kind::Kick && kind <= Kind::Whitelist && m_state.permissionOf(client) != QcPermission::Player) {
        client.connection->send(m_reports.lists());
        client.connection->send(m_reports.players(pings()));
    }
}

void QcServerAdmin::pong(const QcServerClient &client, quint32 nonce)
{
    const auto ping = m_pings.find(client.id);
    if (ping != m_pings.end() && ping->waiting && ping->nonce == nonce) {
        ping->lastMs = static_cast<qint32>(std::min<qint64>(ping->sent.elapsed(), std::numeric_limits<qint32>::max()));
        ping->waiting = false;
    }
}

void QcServerAdmin::update()
{
    dropUnauthorized();
    tellRights();
    sendChat();
    if (m_state.clock().playerTimeNs() - m_lastSampleNs >= statusIntervalNs) {
        sampleSecond();
    }
}

void QcServerAdmin::noteLeft(const QcServerClient &client)
{
    const QcTraffic own = client.connection && !client.local ? client.connection->traffic() : QcTraffic{};
    m_departed.sent += own.sent;
    m_departed.received += own.received;
    m_subscribers.remove(client.id);
    m_operators.remove(client.id);
    m_pings.remove(client.id);
    m_lastTail.remove(client.id);
}

QcTraffic QcServerAdmin::traffic() const
{
    QcTraffic total = m_departed;
    for (const auto &client : m_state.clients()) {
        if (client->connection && !client->local) {
            const QcTraffic own = client->connection->traffic();
            total.sent += own.sent;
            total.received += own.received;
        }
    }
    return total;
}

void QcServerAdmin::dropUnauthorized()
{
    for (auto it = m_subscribers.begin(); it != m_subscribers.end();) {
        const auto client = std::ranges::find(m_state.clients(), it.key(), [](const auto &candidate) { return candidate->id; });
        const bool present = client != m_state.clients().end() && (*client)->state == QcServerClient::State::Playing;
        if (present && m_state.permissionOf(**client) != QcPermission::Player) {
            ++it;
            continue;
        }
        if (present) {
            qCInfo(lcAdmin).noquote() << u"%1 lost the operator rights: the server administration ends"_s.arg((*client)->name);
            (*client)->connection->send(refusal(0, QcMsg::AdminResult::Refusal::Rights, rightsText));
            // That refusal tells him already (tellRights).
            m_operators.remove(it.key());
        }
        it = m_subscribers.erase(it);
    }
}

void QcServerAdmin::tellRights()
{
    for (const auto &client : m_state.clients()) {
        if (client->state != QcServerClient::State::Playing || !client->connection) {
            continue;
        }

        const bool operatorRights = m_state.permissionOf(*client) != QcPermission::Player;
        if (operatorRights == m_operators.contains(client->id)) {
            continue;
        }
        if (operatorRights) {
            m_operators.insert(client->id);
            client->connection->send(QcMsg::AdminResult{0, true, {}});
        } else {
            m_operators.remove(client->id);
            client->connection->send(refusal(0, QcMsg::AdminResult::Refusal::Rights, rightsText));
        }
    }
}

void QcServerAdmin::sendChat()
{
    const quint64 latest = m_commands.chatLog().latest();
    for (auto it = m_subscribers.begin(); it != m_subscribers.end(); ++it) {
        if ((it->topics & QcMsg::AdminSubscribe::chat) == 0 || it->chatSeen >= latest) {
            continue;
        }
        for (const auto &client : m_state.clients()) {
            if (client->id == it.key()) {
                client->connection->send(QcMsg::AdminChat{false, m_commands.chatLog().since(it->chatSeen)});
            }
        }
        it->chatSeen = latest;
    }
}

void QcServerAdmin::sampleSecond()
{
    const qint64 now = m_state.clock().playerTimeNs();
    const double seconds = m_lastSampleNs > 0 ? static_cast<double>(now - m_lastSampleNs) / 1e9 : 1.0;
    m_lastSampleNs = now;

    QcAdminStatusRecorder::Counts counts;
    counts.timeMs = QDateTime::currentMSecsSinceEpoch();
    counts.players = static_cast<quint32>(std::ranges::count_if(m_state.clients(), [](const auto &client) { return client->joined; }));
    counts.loadedChunks = static_cast<quint32>(m_state.world().loadedChunkCount());
    for (const quint32 count : m_reports.entitiesByKind()) {
        counts.entities += count;
    }
    counts.memoryMb = static_cast<float>(QcProcessMemory::currentMb());
    const QcTraffic bytes = traffic();
    counts.sentBytes = bytes.sent;
    counts.receivedBytes = bytes.received;
    const QcMsg::AdminSample sample = m_recorder.sample(counts, seconds);

    bool following = false;
    for (auto it = m_subscribers.cbegin(); it != m_subscribers.cend(); ++it) {
        following = following || (it->topics & QcMsg::AdminSubscribe::status) != 0;
    }
    if (following) {
        const QcMsg::AdminStatus status = m_reports.status(sample, static_cast<quint64>(m_uptime.elapsed()));
        const QcMsg::AdminPlayers players = m_reports.players(pings());
        for (const auto &client : m_state.clients()) {
            if ((m_subscribers.value(client->id).topics & QcMsg::AdminSubscribe::status) != 0) {
                client->connection->send(status);
                client->connection->send(players);
            }
        }
        sendPings();
    }
    sendLog();
}

void QcServerAdmin::sendLog()
{
    // A quick look first: nothing new, nothing to collect.
    const quint64 newest = QcMessageLog::latestSequence();
    for (const auto &client : m_state.clients()) {
        const auto subscriber = m_subscribers.find(client->id);
        if (subscriber == m_subscribers.end() || (subscriber->topics & QcMsg::AdminSubscribe::log) == 0 || subscriber->logSeen >= newest) {
            continue;
        }
        // The lines and the number of the newest come from one look at the log, so a line written
        // meanwhile comes with the next second, not twice.
        const QcAdminReports::Log lines = m_reports.log(subscriber->logLevel, Limits::maxLogLines, subscriber->logSeen, false);
        subscriber->logSeen = lines.latest;
        if (!lines.message.lines.isEmpty()) {
            client->connection->send(lines.message);
        }
    }
}

void QcServerAdmin::sendPings()
{
    const qint64 now = m_state.clock().playerTimeNs();
    if (now - m_lastPingNs < pingIntervalNs) {
        return;
    }
    m_lastPingNs = now;
    for (const auto &client : m_state.clients()) {
        if (client->state != QcServerClient::State::Playing) {
            continue;
        }
        // A ping without an answer is sent again; its round trip stays the last one known.
        Ping &ping = m_pings[client->id];
        ++ping.nonce;
        ping.sent.start();
        ping.waiting = true;
        client->connection->send(QcMsg::Ping{ping.nonce});
    }
}

QHash<quint32, qint32> QcServerAdmin::pings() const
{
    QHash<quint32, qint32> result;
    for (auto it = m_pings.cbegin(); it != m_pings.cend(); ++it) {
        result.insert(it.key(), it->lastMs);
    }
    return result;
}
