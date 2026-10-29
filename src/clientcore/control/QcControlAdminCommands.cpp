// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/control/QcControlAdminCommands.h"

#include "clientcore/control/QcControlCommandTable.h"
#include "clientcore/control/QcControlContext.h"
#include "clientcore/session/QcClientSession.h"
#include "common/core/QcCommandSyntax.h"
#include "common/gameplay/QcGameMode.h"
#include "common/protocol/QcChatText.h"

#include <QJsonArray>

#include <array>

using namespace Qt::StringLiterals;
using Kind = QcMsg::AdminRequest::Kind;

namespace {

constexpr std::array<const char *, QcMsg::AdminRequest::kindCount> kindNames{
    "catalogue", "players", "lists",           "settings",  "log",      "command", "chat",  "kick", "ban",    "unban",
    "op",        "deop",    "whitelistAdd",    "whitelistRemove", "whitelist", "gamerule", "limit", "save", "backup", "stop"};

constexpr std::array<const char *, QcMsg::AdminLogLine::levelCount> levelNames{"debug", "info", "warning", "critical", "fatal"};

QJsonArray texts(const QStringList &list)
{
    return QJsonArray::fromStringList(list);
}

QJsonObject sampleJson(const QcMsg::AdminSample &s)
{
    return {{u"timeMs"_s, s.timeMs},
            {u"tickMeanMs"_s, s.tickMeanMs},
            {u"tickP99Ms"_s, s.tickP99Ms},
            {u"tickMaxMs"_s, s.tickMaxMs},
            {u"ticksPerSecond"_s, s.ticksPerSecond},
            {u"players"_s, static_cast<qint64>(s.players)},
            {u"loadedChunks"_s, static_cast<qint64>(s.loadedChunks)},
            {u"entities"_s, static_cast<qint64>(s.entities)},
            {u"memoryMb"_s, s.memoryMb},
            {u"sentKbPerSecond"_s, s.sentKbPerSecond},
            {u"receivedKbPerSecond"_s, s.receivedKbPerSecond}};
}

QJsonArray settingsArray(const QList<QcMsg::AdminSettings::Setting> &settings)
{
    QJsonArray array;
    for (const QcMsg::AdminSettings::Setting &s : settings) {
        array.append(QJsonObject{{u"key"_s, s.key},
                                 {u"group"_s, s.group},
                                 {u"description"_s, s.description},
                                 {u"type"_s, s.type == QcMsg::AdminSettings::Type::Boolean ? u"boolean"_s : u"integer"_s},
                                 {u"low"_s, s.low},
                                 {u"high"_s, s.high},
                                 {u"default"_s, s.defaultValue},
                                 {u"value"_s, s.value}});
    }
    return array;
}

QJsonArray formsJson(const QString &usage)
{
    QJsonArray forms;
    const QList<QcCommandSyntax::Form> parsed = QcCommandSyntax::forms(usage);
    for (const QcCommandSyntax::Form &form : parsed) {
        QJsonArray parameters;
        for (const QcCommandSyntax::Parameter &p : form.parameters) {
            static constexpr std::array<const char *, 3> kinds{"literal", "required", "optional"};
            parameters.append(QJsonObject{{u"name"_s, p.name},
                                          {u"kind"_s, QLatin1StringView(kinds[static_cast<std::size_t>(p.kind)])},
                                          {u"type"_s, QcCommandSyntax::typeName(p.type)},
                                          {u"choices"_s, texts(p.choices)}});
        }
        forms.append(parameters);
    }
    return forms;
}

} // namespace

QcControlAdminCommands::QcControlAdminCommands(QcControlContext &context)
    : m_context(context)
{
}

void QcControlAdminCommands::registerCommands(QcControlCommandTable &table)
{
    table.add(u"admin"_s, [this](const QJsonObject &request) { admin(request); });
    table.add(u"adminFollow"_s, [this](const QJsonObject &request) { follow(request); });
    table.add(u"adminStatus"_s, [this](const QJsonObject &request) { status(request); });
}

std::optional<Kind> QcControlAdminCommands::kindNamed(const QString &name)
{
    for (std::size_t i = 0; i < kindNames.size(); ++i) {
        if (name == QLatin1StringView(kindNames[i])) {
            return static_cast<Kind>(i);
        }
    }
    return std::nullopt;
}

QcClientSession &QcControlAdminCommands::session() const
{
    return *m_context.session();
}

void QcControlAdminCommands::admin(const QJsonObject &request)
{
    const std::optional<Kind> kind = kindNamed(request.value(u"request"_s).toString());
    const QJsonValue args = request.value(u"args"_s);
    if (!kind || (!args.isUndefined() && !args.isArray())) {
        m_context.fail(request, u"'request' must name an admin request and 'args' be a list of texts"_s);
        return;
    }
    QStringList arguments;
    const QJsonArray values = args.toArray();
    for (const auto &value : values) {
        arguments.append(value.toVariant().toString());
    }

    const quint32 id = session().admin().request(*kind, arguments);
    if (id == 0) {
        m_context.fail(request, u"The session has no connection to a server"_s);
        return;
    }
    m_context.begin({request, QcControlActiveCommand::timeoutPolls(answerTicks),
                     [this, id] {
                         const std::optional<QcMsg::AdminResult> result = session().admin().result(id);
                         if (result && !result->ok) {
                             m_context.setActiveError(QcClientAdmin::display(*result).join(u'\n'));
                         }
                         return result.has_value();
                     },
                     [this, id, kind] { return resultJson(id, *kind); }});
}

void QcControlAdminCommands::follow(const QJsonObject &request)
{
    quint8 topics = 0;
    const QJsonArray topicNames = request.value(u"topics"_s).toArray();
    for (const auto &value : topicNames) {
        const QString topic = value.toString();
        topics |= topic == u"status"_s ? QcMsg::AdminSubscribe::status
                  : topic == u"chat"_s ? QcMsg::AdminSubscribe::chat
                  : topic == u"log"_s  ? QcMsg::AdminSubscribe::log
                                       : 0;
    }
    const QString levelName = request.value(u"level"_s).toString(u"info"_s);
    const auto level = std::ranges::find_if(levelNames, [&levelName](const char *name) { return levelName == QLatin1StringView(name); });
    if (level == levelNames.end()) {
        m_context.fail(request, u"'level' must be debug, info, warning, critical or fatal"_s);
        return;
    }
    session().admin().follow(topics, static_cast<QcMsg::AdminLogLine::Level>(level - levelNames.begin()));
    m_context.finish(request, {{u"topics"_s, static_cast<int>(session().admin().topics())}});
}

void QcControlAdminCommands::status(const QJsonObject &request)
{
    // Waits for the first status when the status is followed.
    const bool following = (session().admin().topics() & QcMsg::AdminSubscribe::status) != 0;
    m_context.begin({request, QcControlActiveCommand::timeoutPolls(answerTicks),
                     [this, following] { return !following || session().admin().status().has_value(); },
                     [this] { return statusJson(); }});
}

QJsonObject QcControlAdminCommands::resultJson(quint32 id, Kind kind) const
{
    const std::optional<QcMsg::AdminResult> result = session().admin().result(id);
    QJsonObject json{{u"lines"_s, texts(result ? QcClientAdmin::display(*result) : QStringList{})}};
    switch (kind) {
    case Kind::Catalogue:
        json.insert(u"catalogue"_s, catalogueJson());
        break;
    case Kind::Players:
        json.insert(u"players"_s, playersJson());
        break;
    case Kind::Lists:
        json.insert(u"lists"_s, listsJson());
        break;
    case Kind::Settings:
        json.insert(u"settings"_s, settingsJson());
        break;
    case Kind::LogTail:
        json.insert(u"log"_s, logJson());
        break;
    default:
        break;
    }
    return json;
}

QJsonObject QcControlAdminCommands::statusJson() const
{
    const QcClientAdmin &admin = session().admin();
    QJsonObject json{{u"topics"_s, static_cast<int>(admin.topics())},
                     {u"historySeconds"_s, static_cast<int>(admin.history().seconds.size())},
                     {u"historyMinutes"_s, static_cast<int>(admin.history().minutes.size())},
                     {u"players"_s, playersJson()},
                     {u"log"_s, logJson()}};
    QJsonArray chat;
    for (const QcMsg::AdminChatLine &line : admin.chat()) {
        chat.append(QJsonObject{{u"sender"_s, line.sender}, {u"text"_s, QcChatText::display(line.message)}});
    }
    json.insert(u"chat"_s, chat);
    if (const std::optional<QcMsg::AdminStatus> &status = admin.status()) {
        QJsonObject entities;
        for (int kind = 0; kind < QcEntityKinds::count; ++kind) {
            entities.insert(QcEntityKinds::name(static_cast<QcEntityKind>(kind)), static_cast<qint64>(status->entities[static_cast<std::size_t>(kind)]));
        }
        json.insert(u"status"_s, QJsonObject{{u"sample"_s, sampleJson(status->sample)},
                                              {u"entities"_s, entities},
                                              {u"uptimeMs"_s, static_cast<qint64>(status->uptimeMs)},
                                              {u"tick"_s, static_cast<qint64>(status->tick)},
                                              {u"world"_s, status->worldName},
                                              {u"seed"_s, QString::number(status->seed)},
                                              {u"generator"_s, status->generator},
                                              {u"version"_s, status->version},
                                              {u"protocol"_s, static_cast<qint64>(status->protocol)}});
    }
    return json;
}

QJsonObject QcControlAdminCommands::catalogueJson() const
{
    QJsonArray commands;
    for (const QcMsg::AdminCatalogue::Command &command : session().admin().catalogue().commands) {
        static constexpr std::array<const char *, 3> permissions{"player", "operator", "console"};
        commands.append(QJsonObject{{u"name"_s, command.name},
                                    {u"aliases"_s, texts(command.aliases)},
                                    {u"permission"_s, QLatin1StringView(permissions[std::min<std::size_t>(command.permission, 2)])},
                                    {u"needsPlayer"_s, command.needsPlayer},
                                    {u"macro"_s, command.macro},
                                    {u"usage"_s, command.usage},
                                    {u"description"_s, command.description},
                                    {u"forms"_s, formsJson(command.usage)}});
    }
    return {{u"commands"_s, commands}};
}

QJsonObject QcControlAdminCommands::listsJson() const
{
    const QcMsg::AdminLists &lists = session().admin().lists();
    QJsonArray bans;
    for (const QcMsg::AdminLists::Ban &ban : lists.bans) {
        bans.append(QJsonObject{{u"name"_s, ban.name}, {u"reason"_s, ban.reason}, {u"by"_s, ban.by}, {u"sinceMs"_s, ban.sinceMs}});
    }
    return {{u"whitelistEnabled"_s, lists.whitelistEnabled},
            {u"operators"_s, texts(lists.operators)},
            {u"whitelist"_s, texts(lists.whitelist)},
            {u"bans"_s, bans}};
}

QJsonObject QcControlAdminCommands::settingsJson() const
{
    const QcMsg::AdminSettings &settings = session().admin().settings();
    return {{u"rules"_s, settingsArray(settings.rules)},
            {u"limits"_s, settingsArray(settings.limits)},
            {u"limitsFrom"_s, settings.limitsFromServer ? u"server.json"_s : u"world.json"_s}};
}

QJsonObject QcControlAdminCommands::playersJson() const
{
    QJsonArray players;
    for (const QcMsg::AdminPlayers::Entry &p : session().admin().players()) {
        players.append(QJsonObject{{u"entity"_s, static_cast<qint64>(p.entityId)},
                                   {u"name"_s, p.name},
                                   {u"pingMs"_s, p.pingMs},
                                   {u"position"_s, QJsonArray{p.position.x, p.position.y, p.position.z}},
                                   {u"gameMode"_s, QcGameModes::name(p.gameMode)},
                                   {u"operator"_s, p.operatorRights},
                                   {u"local"_s, p.local},
                                   {u"connectedMs"_s, p.connectedMs}});
    }
    return {{u"players"_s, players}};
}

QJsonObject QcControlAdminCommands::logJson() const
{
    QJsonArray lines;
    for (const QcMsg::AdminLogLine &line : session().admin().log()) {
        lines.append(QJsonObject{{u"timeMs"_s, line.timeMs},
                                 {u"level"_s, QLatin1StringView(levelNames[static_cast<std::size_t>(line.level)])},
                                 {u"category"_s, line.category},
                                 {u"text"_s, line.text}});
    }
    return {{u"lines"_s, lines}};
}
