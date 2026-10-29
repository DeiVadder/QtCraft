// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/commands/QcCommandHelp.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/protocol/QcChatText.h"

#include <algorithm>

using namespace Qt::StringLiterals;

QcCommandHelp::Texts QcCommandHelp::textsOf(QcCommandCategory category)
{
    switch (category) {
    case QcCommandCategory::General:
        return {QT_TRANSLATE_NOOP("QcServerMessages", "General (/help general): %1"), QT_TRANSLATE_NOOP("QcServerMessages", "General commands, page %1 of %2:")};
    case QcCommandCategory::Players:
        return {QT_TRANSLATE_NOOP("QcServerMessages", "Players (/help players): %1"), QT_TRANSLATE_NOOP("QcServerMessages", "Player commands, page %1 of %2:")};
    case QcCommandCategory::Items:
        return {QT_TRANSLATE_NOOP("QcServerMessages", "Items and health (/help items): %1"),
                QT_TRANSLATE_NOOP("QcServerMessages", "Commands for items and health, page %1 of %2:")};
    case QcCommandCategory::Creatures:
        return {QT_TRANSLATE_NOOP("QcServerMessages", "Creatures (/help creatures): %1"),
                QT_TRANSLATE_NOOP("QcServerMessages", "Creature commands, page %1 of %2:")};
    case QcCommandCategory::Building:
        return {QT_TRANSLATE_NOOP("QcServerMessages", "Building (/help building): %1"),
                QT_TRANSLATE_NOOP("QcServerMessages", "Building commands, page %1 of %2:")};
    case QcCommandCategory::World:
        return {QT_TRANSLATE_NOOP("QcServerMessages", "World and rules (/help world): %1"),
                QT_TRANSLATE_NOOP("QcServerMessages", "Commands for the world and its rules, page %1 of %2:")};
    case QcCommandCategory::Finding:
        return {QT_TRANSLATE_NOOP("QcServerMessages", "Finding places (/help finding): %1"),
                QT_TRANSLATE_NOOP("QcServerMessages", "Commands for finding places, page %1 of %2:")};
    case QcCommandCategory::Server:
        return {QT_TRANSLATE_NOOP("QcServerMessages", "Server (/help server): %1"), QT_TRANSLATE_NOOP("QcServerMessages", "Server commands, page %1 of %2:")};
    case QcCommandCategory::Other:
        break;
    }
    return {QT_TRANSLATE_NOOP("QcServerMessages", "More commands (/help other): %1"), QT_TRANSLATE_NOOP("QcServerMessages", "More commands, page %1 of %2:")};
}

QcCommandHelp::QcCommandHelp(const QcCommandRegistry &registry, const QcContentRegistry &content)
    : m_registry(registry)
    , m_content(content)
{
}

bool QcCommandHelp::mayRun(const QcCommandSource &source, const QcCommandMacroDefinition &macro)
{
    return source.may(macro.operatorOnly ? QcPermission::Operator : QcPermission::Player);
}

void QcCommandHelp::run(const QcCommandContext &context) const
{
    const QcCommandSource &source = context.source;
    if (context.args.isEmpty()) {
        overview(source);
        return;
    }

    const QString word = context.args[0].startsWith(u'/') ? context.args[0].mid(1).toLower() : context.args[0].toLower();
    if (const std::optional<QcCommandCategory> category = QcCommandCategories::fromId(word); category && !context.args[0].startsWith(u'/')) {
        page(source, *category, context.args);
        return;
    }
    if (!explain(source, word)) {
        source.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Unknown command or group: %1 (/help lists them)"), {context.args[0]});
    }
}

QList<QcCommandHelp::Entry> QcCommandHelp::visible(const QcCommandSource &source) const
{
    QList<Entry> entries;
    const QList<const QcCommandSpec *> commands = m_registry.commands();
    for (const QcCommandSpec *spec : commands) {
        if (source.may(spec->permission) && (source.player() || !spec->needsPlayer)) {
            entries.append({spec->name, spec->category, spec->description, {}});
        }
    }
    for (const QcCommandMacroDefinition &macro : m_content.commandMacros()) {
        if (!m_registry.find(macro.name) && mayRun(source, macro)) {
            entries.append({macro.name, QcCommandCategory::Other, nullptr, macro.description});
        }
    }
    std::ranges::sort(entries, {}, &Entry::name);
    return entries;
}

void QcCommandHelp::overview(const QcCommandSource &source) const
{
    const QList<Entry> entries = visible(source);
    for (const QcCommandCategory category : QcCommandCategories::all()) {
        QStringList names;
        for (const Entry &entry : entries) {
            if (entry.category == category) {
                names.append(u'/' + entry.name);
            }
        }
        if (!names.isEmpty()) {
            source.reply(textsOf(category).overview, {names.join(u", "_s)});
        }
    }
    source.reply(QT_TRANSLATE_NOOP("QcServerMessages", "/help <group> [page] describes the commands of a group, /help <command> explains one"));
}

void QcCommandHelp::page(const QcCommandSource &source, QcCommandCategory category, const QStringList &args) const
{
    QList<Entry> entries = visible(source);
    entries.removeIf([category](const Entry &entry) { return entry.category != category; });
    const QString id = QcCommandCategories::id(category);
    const qsizetype pages = std::max<qsizetype>(1, (entries.size() + pageSize - 1) / pageSize);
    bool numeric = true;
    const qsizetype number = args.size() > 1 ? args[1].toLongLong(&numeric) : 1;
    if (!numeric || number < 1 || number > pages || args.size() > 2) {
        source.reply(QT_TRANSLATE_NOOP("QcServerMessages", "/help %1 has pages 1 to %2"), {id, QString::number(pages)});
        return;
    }
    if (entries.isEmpty()) {
        source.reply(QT_TRANSLATE_NOOP("QcServerMessages", "No commands of this group for you"));
        return;
    }

    source.reply(textsOf(category).page, {QString::number(number), QString::number(pages)});
    const QList<Entry> shown = entries.mid((number - 1) * pageSize, pageSize);
    for (const Entry &entry : shown) {
        if (entry.description) {
            source.send(QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "/%1: %2"), {entry.name, QString::fromUtf8(entry.description)},
                                            {QString(), QcChatText::messageKey(entry.description)}));
        } else {
            source.replyPlain(u"/%1: %2"_s.arg(entry.name, entry.plainDescription));
        }
    }
    if (number < pages) {
        source.reply(QT_TRANSLATE_NOOP("QcServerMessages", "More with /help %1 %2"), {id, QString::number(number + 1)});
    }
}

bool QcCommandHelp::explain(const QcCommandSource &source, const QString &name) const
{
    // A command the source may not run stays unknown to it, as in the overview (/help locate would
    // name the world's structures to non-operators).
    if (const QcCommandSpec *spec = m_registry.find(name); spec && source.may(spec->permission)) {
        source.reply(spec->usage);
        source.reply(spec->description);
        if (spec->names && spec->namesLine) {
            source.reply(spec->namesLine, {spec->names().join(u", "_s)});
        }
        return true;
    }
    if (const QcCommandMacroDefinition *macro = m_content.commandMacro(name); macro && mayRun(source, *macro)) {
        source.replyPlain(macro->usage);
        source.replyPlain(macro->description);
        return true;
    }
    return false;
}
