#include "server/commands/QcAdventureCommands.h"

#include "server/commands/QcCommandArguments.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcServerGameModes.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

constexpr const char *usage =
    QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /adventure [canBreak|canPlaceOn <item|*> <block>... | remove <canBreak|canPlaceOn> <item|*> [block...] | clear]");

// One sentence per action, so the client translates it as a whole (decision E-094).
const char *listedKey(QcAdventureRules::Action action)
{
    return action == QcAdventureRules::Action::Break ? QT_TRANSLATE_NOOP("QcServerMessages", "%1 may break: %2")
                                                     : QT_TRANSLATE_NOOP("QcServerMessages", "%1 may be placed on: %2");
}

} // namespace

QcAdventureCommands::QcAdventureCommands(QcServerState &state, QcServerGameModes &gameModes)
    : m_state(state)
    , m_gameModes(gameModes)
{
}

void QcAdventureCommands::addTo(QcCommandRegistry &registry)
{
    registry.addBuiltIn({.name = u"adventure"_s, .category = QcCommandCategory::Players,
                         .usage = usage,
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Shows or changes what players in adventure mode may break and place on."),
                         .run = [this](const QcCommandContext &context) { adventure(context); }});
}

void QcAdventureCommands::adventure(const QcCommandContext &context)
{
    const QStringList &args = context.args;
    if (args.isEmpty()) {
        list(context);
        return;
    }

    if (const std::optional<QcAdventureRules::Action> action = QcAdventureRules::parseAction(args[0]); action && args.size() >= 3) {
        allow(context, *action);
    } else if (args[0] == u"remove"_s && args.size() >= 3) {
        remove(context);
    } else if (args[0] == u"clear"_s && args.size() == 1) {
        clear(context);
    } else {
        context.reply(usage);
    }
}

void QcAdventureCommands::list(const QcCommandContext &context) const
{
    const QcAdventureRules &rules = m_state.info().adventure;
    if (rules.isEmpty()) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "No adventure allow-lists: players in adventure mode can neither break nor place blocks"));
        return;
    }

    for (const QcAdventureRules::Action action : {QcAdventureRules::Action::Break, QcAdventureRules::Action::PlaceOn}) {
        const QStringList items = rules.items(action);
        for (const QString &item : items) {
            context.reply(listedKey(action), {item, rules.blocks(action, item).join(u", "_s)});
        }
    }
}

void QcAdventureCommands::allow(const QcCommandContext &context, QcAdventureRules::Action action)
{
    const std::optional<QString> item = itemName(context, context.args[1]);
    const std::optional<QStringList> blocks = item ? blockNames(context, 2) : std::nullopt;
    if (!blocks) {
        return;
    }

    QcAdventureRules &rules = m_state.info().adventure;
    if (!rules.allow(action, *item, *blocks)) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "The adventure allow-lists are full (at most %1 items with %2 blocks each)"),
                      {QString::number(QcAdventureRules::maxItems), QString::number(QcAdventureRules::maxBlocks)});
        return;
    }

    m_gameModes.sendAdventureRules();
    context.reply(listedKey(action), {*item, rules.blocks(action, *item).join(u", "_s)});
}

void QcAdventureCommands::remove(const QcCommandContext &context)
{
    const std::optional<QcAdventureRules::Action> action = QcAdventureRules::parseAction(context.args[1]);
    if (!action) {
        context.reply(usage);
        return;
    }

    // Names the content no longer has (a removed mod) can still be taken off: only their form counts.
    const QString item = listedName(context.args[2]);
    QStringList blocks;
    for (qsizetype i = 3; i < context.args.size(); ++i) {
        blocks.append(listedName(context.args[i]));
    }
    if (!QcAdventureRules::isValidName(item) || !std::ranges::all_of(blocks, QcAdventureRules::isValidName)) {
        context.reply(usage);
        return;
    }

    if (!m_state.info().adventure.remove(*action, item, blocks)) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Nothing to remove for %1"), {item});
        return;
    }

    m_gameModes.sendAdventureRules();
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Removed from the adventure allow-lists of %1"), {item});
}

void QcAdventureCommands::clear(const QcCommandContext &context)
{
    m_state.info().adventure.clear();
    m_gameModes.sendAdventureRules();
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Cleared the adventure allow-lists"));
}

QString QcAdventureCommands::listedName(const QString &word)
{
    return word == QLatin1StringView(QcAdventureRules::anyItem) ? word : QcCommandArguments::qualified(word);
}

std::optional<QString> QcAdventureCommands::itemName(const QcCommandContext &context, const QString &word) const
{
    if (word == QLatin1StringView(QcAdventureRules::anyItem)) {
        return word;
    }

    QString name = QcCommandArguments::qualified(word);
    if (!m_state.content().itemId(name)) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Unknown item: %1"), {word});
        return std::nullopt;
    }
    return name;
}

std::optional<QStringList> QcAdventureCommands::blockNames(const QcCommandContext &context, qsizetype first) const
{
    QStringList names;
    for (qsizetype i = first; i < context.args.size(); ++i) {
        const QString name = QcCommandArguments::qualified(context.args[i]);
        if (!m_state.content().blockId(name)) {
            context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Unknown block: %1"), {context.args[i]});
            return std::nullopt;
        }
        names.append(name);
    }
    return names;
}
