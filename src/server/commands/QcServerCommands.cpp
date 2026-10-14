#include "QcServerCommands.h"

#include "QcChatText.h"
#include "QcServerState.h"

#include <QLoggingCategory>

using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcCommands, "qtcraft.commands")

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
    , m_cheats(state, std::move(parts.cheats))
    , m_blocks(state, parts.blocks, parts.updates, m_pending)
    , m_players(state, m_pending)
    , m_world(state, m_pending, std::move(parts.world))
    , m_ticks(state, std::move(parts.ticks))
{
    addBuiltIns();
    m_cheats.addTo(m_registry);
    m_blocks.addTo(m_registry);
    m_players.addTo(m_registry);
    m_world.addTo(m_registry);
    m_ticks.addTo(m_registry);
}

void QcServerCommands::addBuiltIns()
{
    m_registry.addBuiltIn({.name = u"help"_s, .permission = QcPermission::Player,
                           .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /help [command]"),
                           .description = QT_TRANSLATE_NOOP("QcServerMessages", "Lists the commands you may use, or explains one."),
                           .run = [this](const QcCommandContext &context) { help(context); }});
    m_registry.addBuiltIn({.name = u"confirm"_s, .permission = QcPermission::Player,
                           .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /confirm"),
                           .description = QT_TRANSLATE_NOOP("QcServerMessages", "Runs the destructive command you started last."),
                           .run = [this](const QcCommandContext &context) { m_pending.confirm(context.source, m_state.clock().playerTimeNs()); }});
    m_registry.addBuiltIn({.name = u"cancel"_s, .permission = QcPermission::Player,
                           .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /cancel"),
                           .description = QT_TRANSLATE_NOOP("QcServerMessages", "Drops the command waiting for /confirm."),
                           .run = [this](const QcCommandContext &context) { m_pending.cancel(context.source); }});
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

void QcServerCommands::broadcast(const QcServerClient &client, const QString &text) const
{
    for (const auto &other : m_state.clients()) {
        if (other->state == QcServerClient::State::Playing) {
            other->connection->send(QcChatText::plain(u"<%1> %2"_s.arg(client.name, text)));
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

bool QcServerCommands::mayRun(const QcCommandSource &source, const QcCommandMacroDefinition &macro)
{
    return source.may(macro.operatorOnly ? QcPermission::Operator : QcPermission::Player);
}

void QcServerCommands::help(const QcCommandContext &context) const
{
    const QcCommandSource &source = context.source;
    if (!context.args.isEmpty()) {
        const QString name = context.args[0].startsWith(u'/') ? context.args[0].mid(1).toLower() : context.args[0].toLower();
        if (const QcCommandSpec *spec = m_registry.find(name)) {
            source.reply(spec->usage);
            source.reply(spec->description);
        } else if (const QcCommandMacroDefinition *macro = m_state.content().commandMacro(name); macro && mayRun(source, *macro)) {
            source.replyPlain(macro->usage);
            source.replyPlain(macro->description);
        } else {
            source.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Unknown command: %1"), {context.args[0]});
        }
        return;
    }

    QStringList names;
    const QList<const QcCommandSpec *> commands = m_registry.commands();
    for (const QcCommandSpec *spec : commands) {
        if (source.may(spec->permission) && (source.player() || !spec->needsPlayer)) {
            names.append(u'/' + spec->name);
        }
    }
    for (const QcCommandMacroDefinition &macro : m_state.content().commandMacros()) {
        if (!m_registry.find(macro.name) && mayRun(source, macro)) {
            names.append(u'/' + macro.name);
        }
    }
    source.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Commands: %1"), {names.join(u", "_s)});
    source.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Type /help <command> for its usage"));
}
