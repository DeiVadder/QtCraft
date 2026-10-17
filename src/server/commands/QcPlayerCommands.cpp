#include "server/commands/QcPlayerCommands.h"

#include "common/protocol/QcChatText.h"
#include "server/chunks/QcChunkStreaming.h"
#include "server/commands/QcCommandArguments.h"
#include "server/commands/QcPendingCommands.h"
#include "server/core/QcProfileValidation.h"
#include "server/core/QcServerState.h"

#include <QLoggingCategory>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcPlayers, "qtcraft.commands")

QString reasonOf(const QStringList &args, qsizetype first)
{
    return args.mid(first).join(u' ').left(QcAccessList::maxReasonLength);
}

// The cleaned player name of a command word; a word that names nobody (empty once cleaned, only
// punctuation) gets an error instead of standing for "Player".
[[nodiscard]] std::optional<QString> nameOf(const QcCommandContext &context, const QString &word)
{
    const std::optional<QString> name = QcProfileValidation::validPlayerName(word);
    if (!name) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Not a valid player name: %1"), {word});
    }
    return name;
}

} // namespace

QcPlayerCommands::QcPlayerCommands(QcServerState &state, QcPendingCommands &pending)
    : m_state(state)
    , m_pending(pending)
{
}

void QcPlayerCommands::addTo(QcCommandRegistry &registry)
{
    registry.addBuiltIn({.name = u"list"_s, .permission = QcPermission::Player,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /list"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Lists the players online."),
                         .run = [this](const QcCommandContext &context) { list(context); }});
    registry.addBuiltIn({.name = u"kick"_s, .minArguments = 1,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /kick <player> [reason]"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Disconnects a player; he may join again."),
                         .run = [this](const QcCommandContext &context) { kick(context); }});
    registry.addBuiltIn({.name = u"ban"_s, .minArguments = 1,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /ban <player> [reason]"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Keeps a player name out of this world and disconnects him."),
                         .run = [this](const QcCommandContext &context) { ban(context); }});
    registry.addBuiltIn({.name = u"unban"_s, .aliases = {u"pardon"_s}, .minArguments = 1,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /unban <player>"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Lets a banned player join again."),
                         .run = [this](const QcCommandContext &context) { unban(context); }});
    registry.addBuiltIn({.name = u"tp"_s, .aliases = {u"teleport"_s}, .minArguments = 1,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /tp <x> <y> <z>, /tp <player>, /tp <player> <x> <y> <z> or /tp <player> <target player>"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Moves you or a player to a position or to another player."),
                         .run = [this](const QcCommandContext &context) { teleport(context); }});
    addListCommands(registry);
}

void QcPlayerCommands::addListCommands(QcCommandRegistry &registry)
{
    registry.addBuiltIn({.name = u"banlist"_s,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /banlist"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Lists the banned names with their reasons."),
                         .run = [this](const QcCommandContext &context) { banList(context); }});
    registry.addBuiltIn({.name = u"whitelist"_s, .minArguments = 1,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /whitelist <on|off|list|add <player>|remove <player>>"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages",
                                                          "With the whitelist on, only listed players and operators may join."),
                         .run = [this](const QcCommandContext &context) { whitelist(context); }});
    registry.addBuiltIn({.name = u"op"_s,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /op [player]"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Makes a player an operator, or lists the operators."),
                         .run = [this](const QcCommandContext &context) { op(context, true); }});
    registry.addBuiltIn({.name = u"deop"_s, .minArguments = 1,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /deop <player>"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Takes the operator rights from a player."),
                         .run = [this](const QcCommandContext &context) { op(context, false); }});
}

void QcPlayerCommands::list(const QcCommandContext &context) const
{
    QStringList names;
    for (const auto &client : m_state.clients()) {
        if (client->joined && client->state == QcServerClient::State::Playing) {
            names.append(client->name);
        }
    }
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "%1 players online: %2"), {QString::number(names.size()), names.join(u", "_s)});
}

int QcPlayerCommands::disconnect(const QString &name, const QcMsg::Disconnect &reason, bool &hostRefused)
{
    int count = 0;
    hostRefused = false;
    const QList<QcServerClient *> online = QcCommandArguments::players(m_state, name);
    for (QcServerClient *player : online) {
        if (player->local) {
            hostRefused = true;
            continue;
        }
        player->connection->send(reason);
        player->connection->close();
        ++count;
    }
    return count;
}

void QcPlayerCommands::kick(const QcCommandContext &context)
{
    const std::optional<QString> valid = nameOf(context, context.args[0]);
    if (!valid) {
        return;
    }

    const QString &name = context.args[0];
    const QString reason = reasonOf(context.args, 1);
    bool hostRefused = false;
    const int count = disconnect(name,
                                 reason.isEmpty() ? QcChatText::disconnect(QT_TRANSLATE_NOOP("QcServerMessages", "Kicked by %1"), {context.source.name()})
                                                  : QcChatText::disconnect(QT_TRANSLATE_NOOP("QcServerMessages", "Kicked by %1: %2"), {context.source.name(), reason}),
                                 hostRefused);
    if (hostRefused) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "The host of the world cannot be kicked or banned"));
    } else if (count == 0) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "No player named %1 is online"), {name});
    } else {
        qCInfo(lcPlayers).noquote() << u"%1 kicked %2 (%3)"_s.arg(context.source.name(), name, reason);
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Kicked %1"), {name});
    }
}

void QcPlayerCommands::ban(const QcCommandContext &context)
{
    const std::optional<QString> valid = nameOf(context, context.args[0]);
    if (!valid || refusedAsFull(context, QcAccessList::List::Bans, *valid)) {
        return;
    }

    const QString &name = context.args[0];
    if (std::ranges::any_of(QcCommandArguments::players(m_state, name), [](const QcServerClient *player) { return player->local; })) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "The host of the world cannot be kicked or banned"));
        return;
    }

    const QString reason = reasonOf(context.args, 1);
    const QcResult<bool> changed = m_state.access().addBan({name, reason, context.source.name(), QDateTime::currentDateTimeUtc(), fingerprintOf(name)});
    bool hostRefused = false;
    disconnect(name,
               reason.isEmpty() ? QcChatText::disconnect(QT_TRANSLATE_NOOP("QcServerMessages", "Banned by %1"), {context.source.name()})
                                : QcChatText::disconnect(QT_TRANSLATE_NOOP("QcServerMessages", "Banned by %1: %2"), {context.source.name(), reason}),
               hostRefused);
    qCInfo(lcPlayers).noquote() << u"%1 banned %2 (%3)"_s.arg(context.source.name(), name, reason);
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Banned %1"), {*valid});
    reportWrite(context, changed);
}

void QcPlayerCommands::unban(const QcCommandContext &context)
{
    if (!nameOf(context, context.args[0])) {
        return;
    }

    const QcResult<bool> changed = m_state.access().removeBan(context.args[0]);
    if (changed && !*changed) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "%1 is not banned"), {context.args[0]});
        return;
    }
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Unbanned %1"), {context.args[0]});
    reportWrite(context, changed);
}

void QcPlayerCommands::banList(const QcCommandContext &context) const
{
    const QList<QcAccessList::Ban> bans = m_state.access().bans();
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "%1 banned names"), {QString::number(bans.size())});
    for (const QcAccessList::Ban &ban : bans) {
        context.source.replyPlain(u"%1 (%2, %3): %4"_s.arg(ban.name, ban.by, ban.since.toString(Qt::ISODate), ban.reason));
    }
}

void QcPlayerCommands::whitelist(const QcCommandContext &context)
{
    const QStringList &args = context.args;
    const std::optional<bool> toggle = QcCommandArguments::toggle(args[0]);
    QcResult<bool> changed = false;
    if (toggle && args.size() == 1) {
        changed = m_state.access().setWhitelistEnabled(*toggle);
        context.reply(*toggle ? QT_TRANSLATE_NOOP("QcServerMessages", "The whitelist is on") : QT_TRANSLATE_NOOP("QcServerMessages", "The whitelist is off"));
    } else if (args[0] == u"list"_s && args.size() == 1) {
        context.reply(m_state.access().whitelistEnabled() ? QT_TRANSLATE_NOOP("QcServerMessages", "The whitelist is on: %1")
                                                        : QT_TRANSLATE_NOOP("QcServerMessages", "The whitelist is off: %1"),
                      {m_state.access().whitelist().join(u", "_s)});
    } else if ((args[0] == u"add"_s || args[0] == u"remove"_s) && args.size() == 2) {
        const bool add = args[0] == u"add"_s;
        const std::optional<QString> name = nameOf(context, args[1]);
        if (!name || (add && refusedAsFull(context, QcAccessList::List::Whitelist, *name))) {
            return;
        }
        changed = m_state.access().setWhitelisted(*name, add, fingerprintOf(*name));
        context.reply(add ? QT_TRANSLATE_NOOP("QcServerMessages", "%1 is on the whitelist") : QT_TRANSLATE_NOOP("QcServerMessages", "%1 is not on the whitelist"),
                      {*name});
    } else {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /whitelist <on|off|list|add <player>|remove <player>>"));
        return;
    }
    reportWrite(context, changed);
}

void QcPlayerCommands::op(const QcCommandContext &context, bool grant)
{
    if (context.args.isEmpty()) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Operators: %1"), {m_state.access().operators().join(u", "_s)});
        return;
    }

    const std::optional<QString> valid = nameOf(context, context.args[0]);
    if (!valid || (grant && refusedAsFull(context, QcAccessList::List::Operators, *valid))) {
        return;
    }

    const QString &name = *valid;
    const QList<QcServerClient *> online = QcCommandArguments::players(m_state, name);
    if (!grant && std::ranges::any_of(online, [](const QcServerClient *player) { return player->local; })) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "The host of the world is always an operator"));
        return;
    }

    const QcResult<bool> changed = m_state.access().setOperator(name, grant, fingerprintOf(name));
    m_state.notePlayerListChanged();
    qCInfo(lcPlayers).noquote() << u"%1 %2 %3"_s.arg(context.source.name(), grant ? u"made an operator:"_s : u"took operator rights from"_s, name);
    for (QcServerClient *player : online) {
        player->connection->send(QcChatText::message(grant ? QT_TRANSLATE_NOOP("QcServerMessages", "You are now an operator")
                                                           : QT_TRANSLATE_NOOP("QcServerMessages", "You are no longer an operator")));
        if (!grant) {
            // Server-wide debug data is for operators only (decision E-225), and a destructive
            // command waiting for /confirm goes with the rights.
            player->view.debugFlags = 0;
            m_pending.forget(player->id);
        }
    }
    context.reply(grant ? QT_TRANSLATE_NOOP("QcServerMessages", "%1 is an operator") : QT_TRANSLATE_NOOP("QcServerMessages", "%1 is no operator"), {name});
    reportWrite(context, changed);
}

QString QcPlayerCommands::fingerprintOf(const QString &name) const
{
    // The identity of a player online, else the one registered for the name (decision E-253).
    const QList<QcServerClient *> players = QcCommandArguments::players(m_state, name);
    for (const QcServerClient *player : std::as_const(players)) {
        if (!player->login.fingerprint.isEmpty()) {
            return player->login.fingerprint;
        }
    }
    return m_state.identities().fingerprintOf(name);
}

bool QcPlayerCommands::refusedAsFull(const QcCommandContext &context, QcAccessList::List list, const QString &name) const
{
    if (!m_state.access().isFullFor(list, name)) {
        return false;
    }

    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "The list is full (at most %1 names)"), {QString::number(QcAccessList::maxEntries)});
    return true;
}

void QcPlayerCommands::reportWrite(const QcCommandContext &context, const QcResult<bool> &changed)
{
    if (!changed) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "The change applies but could not be saved: %1"), {changed.error()});
    }
}

void QcPlayerCommands::teleport(const QcCommandContext &context)
{
    const QStringList &args = context.args;
    const QcServerClient *self = context.source.player();
    const std::optional<QcVec3> origin = self ? std::optional<QcVec3>(self->motion.position) : std::nullopt;

    // /tp x y z and /tp target move the player himself; with four or two words the first names who.
    const bool named = args.size() == 4 || args.size() == 2;
    QList<QcServerClient *> who = named ? QcCommandArguments::players(m_state, args[0])
                                        : (self ? QList<QcServerClient *>{context.source.player()} : QList<QcServerClient *>{});
    const qsizetype targetIndex = named ? 1 : 0;
    std::optional<QcVec3> destination;
    if (args.size() - targetIndex == 3) {
        destination = QcCommandArguments::point(args, targetIndex, origin);
    } else if (args.size() - targetIndex == 1) {
        const QList<QcServerClient *> targets = QcCommandArguments::players(m_state, args[targetIndex]);
        destination = targets.isEmpty() ? std::nullopt : std::optional<QcVec3>(targets.first()->motion.position);
    }

    if (who.isEmpty() || !destination) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages",
                                        "Usage: /tp <x> <y> <z>, /tp <player>, /tp <player> <x> <y> <z> or /tp <player> <target player>"));
        return;
    }

    for (QcServerClient *player : who) {
        moveTo(*player, *destination);
    }
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Teleported %1 to %2 %3 %4"),
                  {who.first()->name, QString::number(destination->x, 'f', 1), QString::number(destination->y, 'f', 1),
                   QString::number(destination->z, 'f', 1)});
}

void QcPlayerCommands::moveTo(QcServerClient &player, const QcVec3 &position)
{
    // Loaded at once so the player does not fall through a missing chunk; the client takes the new
    // position from the next PlayerState like after a respawn.
    m_state.world().loadChunk(QcChunkStreaming::chunkOf(position));
    player.motion.position = position;
    player.motion.velocity = {};
    player.previousPosition.reset();
    player.fallPeak = position.y;
    player.mining.pos.reset();
    player.stateDirty = true;
}
