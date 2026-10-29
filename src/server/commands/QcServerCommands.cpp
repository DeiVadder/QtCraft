#include "server/commands/QcServerCommands.h"

#include "common/protocol/QcChatText.h"
#include "server/core/QcServerLogging.h"
#include "server/core/QcServerState.h"


using namespace Qt::StringLiterals;

namespace {

QLatin1StringView levelName(QcPermission permission)
{
    switch (permission) {
    case QcPermission::Player:
        return QLatin1StringView("player");
    case QcPermission::Operator:
        return QLatin1StringView("operator");
    case QcPermission::Console:
        return QLatin1StringView("console");
    }
    return QLatin1StringView("player");
}

// A macro line with $1..$9 and $* replaced in one pass (arguments are not scanned again).
QString substitute(const QString &line, const QStringList &args)
{
    QString result;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar next = i + 1 < line.size() ? line[i + 1] : QChar();
        if (line[i] != u'$' || (next != u'*' && (next < u'1' || next > u'9'))) {
            result.append(line[i]);
            continue;
        }

        result.append(next == u'*' ? args.join(u' ') : args.value(next.digitValue() - 1));
        ++i;
    }
    return result;
}

} // namespace

QcServerCommands::QcServerCommands(QcServerState &state, Parts parts)
    : m_state(state)
    , m_adventure(state, parts.cheats.gameModes)
    , m_cheats(state, std::move(parts.cheats))
    , m_blocks(state, parts.blocks, parts.updates, m_pending)
    , m_players(state, m_pending)
    , m_world(state, m_pending, std::move(parts.world))
    , m_ticks(state, std::move(parts.ticks))
    , m_chunks(state)
    , m_weather(state, parts.weather)
    , m_villages(state)
    , m_experience(state)
    , m_advancements(state)
    , m_effects(state)
    , m_mobs(state)
    , m_limits(state)
    , m_help(m_registry, state.content())
{
    addBuiltIns();
    m_cheats.addTo(m_registry);
    m_blocks.addTo(m_registry);
    m_players.addTo(m_registry);
    m_world.addTo(m_registry);
    m_ticks.addTo(m_registry);
    m_chunks.addTo(m_registry);
    m_weather.addTo(m_registry);
    m_villages.addTo(m_registry);
    m_experience.addTo(m_registry);
    m_advancements.addTo(m_registry);
    m_effects.addTo(m_registry);
    m_mobs.addTo(m_registry);
    m_adventure.addTo(m_registry);
    m_limits.addTo(m_registry);
}

void QcServerCommands::addBuiltIns()
{
    m_registry.addBuiltIn({.name = u"help"_s, .category = QcCommandCategory::General, .permission = QcPermission::Player,
                           .usage = QT_TRANSLATE_NOOP("QcServerMessages",
                                                      "Usage: /help [command], /help <general|players|items|creatures|building|world|finding|server|other> [page]"),
                           .description = QT_TRANSLATE_NOOP("QcServerMessages", "Lists the commands you may use by group, or explains one."),
                           .run = [this](const QcCommandContext &context) { m_help.run(context); }});
    m_registry.addBuiltIn({.name = u"confirm"_s, .category = QcCommandCategory::General, .permission = QcPermission::Player,
                           .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /confirm"),
                           .description = QT_TRANSLATE_NOOP("QcServerMessages", "Runs the destructive command you started last."),
                           .run = [this](const QcCommandContext &context) { m_pending.confirm(context.source, m_state.clock().playerTimeNs()); }});
    m_registry.addBuiltIn({.name = u"cancel"_s, .category = QcCommandCategory::General, .permission = QcPermission::Player,
                           .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /cancel"),
                           .description = QT_TRANSLATE_NOOP("QcServerMessages", "Drops the command waiting for /confirm."),
                           .run = [this](const QcCommandContext &context) { m_pending.cancel(context.source); }});
    m_registry.addBuiltIn({.name = u"say"_s, .category = QcCommandCategory::General, .minArguments = 1,
                           .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /say <message>"),
                           .description = QT_TRANSLATE_NOOP("QcServerMessages", "Sends a chat line to everyone as the server."),
                           .run = [this](const QcCommandContext &context) { say(context); }});
}

void QcServerCommands::chat(QcServerClient &client, const QString &text)
{
    if (text.trimmed().isEmpty()) {
        return;
    }
    if (!text.trimmed().startsWith(u'/')) {
        broadcast(client, text);
        return;
    }
    execute(QcCommandSource(client, m_state.permissionOf(client)), text, 0);
}

void QcServerCommands::console(const QString &line, const QcCommandSource::Output &output)
{
    const QString trimmed = line.trimmed().left(256);
    if (trimmed.isEmpty()) {
        return;
    }
    execute(QcCommandSource(output), trimmed.startsWith(u'/') ? trimmed : u'/' + trimmed, 0);
}

void QcServerCommands::admin(QcServerClient &client, const QString &line, const QcCommandSource::Output &output)
{
    // Not cut: the admin channel's requests are bounded (words of at most 256 characters each), and
    // a line it builds ("/say " and 256 characters) must arrive whole.
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    execute(QcCommandSource(client, m_state.permissionOf(client), output), trimmed.startsWith(u'/') ? trimmed : u'/' + trimmed, 0);
}

void QcServerCommands::broadcast(const QcServerClient &client, const QString &text)
{
    sendToAll(client.name, QcChatText::plain(u"<%1> %2"_s.arg(client.name, text)));
}

void QcServerCommands::say(const QcCommandContext &context)
{
    const QString text = context.args.join(u' ');
    qCInfo(lcCommands).noquote() << u"%1 says: %2"_s.arg(context.source.name(), text);
    sendToAll({}, QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "[Server] %1"), {text}));
}

void QcServerCommands::sendToAll(const QString &sender, const QcMsg::ChatMessage &line)
{
    m_chatLog.add(sender, line);
    for (const auto &other : m_state.clients()) {
        if (other->state == QcServerClient::State::Playing) {
            other->connection->send(line);
        }
    }
}

bool QcServerCommands::permitted(const QcCommandSource &source, QcPermission needed, const QString &name) const
{
    if (source.may(needed)) {
        return true;
    }

    qCInfo(lcCommands).noquote() << u"%1 may not use /%2"_s.arg(source.name(), name);
    source.reply(needed == QcPermission::Console ? QT_TRANSLATE_NOOP("QcServerMessages", "Only the server console can use /%1")
                                                 : QT_TRANSLATE_NOOP("QcServerMessages", "Only operators can use /%1"),
                 {name});
    return false;
}

void QcServerCommands::execute(const QcCommandSource &source, const QString &text, int depth)
{
    const QStringList words = text.split(u' ', Qt::SkipEmptyParts);
    const QString name = words.first().mid(1).toLower();
    const QStringList args = words.mid(1);
    qCInfo(lcCommands).noquote() << u"%1 (%2): %3"_s.arg(source.name(), levelName(source.permission()), text);

    const QcCommandSpec *spec = m_registry.find(name);
    if (!spec) {
        const QcCommandMacroDefinition *macro = depth == 0 ? m_state.content().commandMacro(name) : nullptr;
        if (macro && permitted(source, macro->operatorOnly ? QcPermission::Operator : QcPermission::Player, name)) {
            runMacro(source, *macro, args);
        } else if (!macro) {
            source.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Unknown command: %1"), {words.first()});
        }
        return;
    }

    if (!permitted(source, spec->permission, spec->name)) {
        return;
    }
    if (spec->needsPlayer && !source.player()) {
        source.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Only players can use /%1"), {spec->name});
        return;
    }
    if (args.size() < spec->minArguments) {
        source.reply(spec->usage);
        return;
    }

    spec->run({source, args, text, spec->permission});
}

void QcServerCommands::runMacro(const QcCommandSource &source, const QcCommandMacroDefinition &macro, const QStringList &args)
{
    if (args.size() < macro.requiredArguments) {
        source.replyPlain(macro.usage);
        return;
    }
    for (const QString &line : macro.lines) {
        execute(source, substitute(line, args).left(256), 1);
    }
}
