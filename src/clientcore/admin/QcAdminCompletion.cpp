// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/admin/QcAdminCompletion.h"

#include "common/content/registry/QcContentRegistry.h"

#include <algorithm>

using namespace Qt::StringLiterals;
using Type = QcCommandSyntax::Parameter::Type;
using Kind = QcCommandSyntax::Parameter::Kind;

namespace QcAdminCompletion {

namespace {

// The line split into the command, the words after it and the word being typed.
struct Split
{
    QString command;
    QStringList words;
    QString typed;
};

Split split(const QString &line)
{
    const QString text = line.startsWith(u'/') ? line.mid(1) : line;
    QStringList words = text.split(u' ', Qt::SkipEmptyParts);
    Split result;
    if (!text.endsWith(u' ') && !words.isEmpty()) {
        result.typed = words.takeLast();
    }
    // Without a finished first word the command's name is still being typed.
    if (!words.isEmpty()) {
        result.command = words.takeFirst().toLower();
    }
    result.words = words;
    return result;
}

// The names of the definitions that start with `typed`, with or without their pack ("qtcraft:").
template<typename List>
QStringList namesStarting(const List &definitions, const QString &typed)
{
    QStringList names;
    for (const auto &definition : definitions) {
        const QString shortName = definition.name.section(u':', -1);
        if (definition.name.startsWith(typed) || shortName.startsWith(typed)) {
            names.append(definition.name);
        }
    }
    return names;
}

// The words the parameter takes that start with `typed`; `names` are the words the server names
// for the command (structures, biomes).
QStringList candidates(const QcCommandSyntax::Parameter &parameter, const QString &typed, const Sources &sources, const QStringList &names)
{
    QStringList out;
    if (parameter.kind == Kind::Literal) {
        out.append(parameter.name);
    }
    out.append(parameter.choices);
    if (parameter.type == Type::Player) {
        out.append(sources.players);
    } else if (parameter.type == Type::Named) {
        out.append(names);
    } else if (sources.content && parameter.type == Type::Block) {
        out.append(namesStarting(sources.content->blocks(), typed));
    } else if (sources.content && parameter.type == Type::Item) {
        out.append(namesStarting(sources.content->items(), typed));
    } else if (sources.content && parameter.type == Type::Creature) {
        out.append(namesStarting(sources.content->mobs(), typed));
    }
    out.removeIf([&typed](const QString &word) {
        return !word.startsWith(typed, Qt::CaseInsensitive) && !word.section(u':', -1).startsWith(typed, Qt::CaseInsensitive);
    });
    return out;
}

} // namespace

const QcMsg::AdminCatalogue::Command *command(const QcMsg::AdminCatalogue &catalogue, QStringView name)
{
    for (const QcMsg::AdminCatalogue::Command &entry : catalogue.commands) {
        if (entry.name == name || entry.aliases.contains(name)) {
            return &entry;
        }
    }
    return nullptr;
}

QList<QcCommandSyntax::Parameter> parameters(const QString &line, const QcMsg::AdminCatalogue &catalogue)
{
    const Split parts = split(line);
    const QcMsg::AdminCatalogue::Command *found = command(catalogue, parts.command);
    return found ? QcCommandSyntax::next(QcCommandSyntax::forms(found->usage), parts.words) : QList<QcCommandSyntax::Parameter>{};
}

QStringList complete(const QString &line, const Sources &sources, qsizetype limit)
{
    const Split parts = split(line);
    QStringList words;
    QString prefix;
    if (parts.command.isEmpty()) {
        for (const QcMsg::AdminCatalogue::Command &entry : sources.catalogue.commands) {
            words.append(entry.name);
        }
        words.removeIf([&parts](const QString &name) { return !name.startsWith(parts.typed); });
        prefix = u"/"_s;
    } else {
        const QcMsg::AdminCatalogue::Command *found = command(sources.catalogue, parts.command);
        const QList<QcCommandSyntax::Parameter> next = found ? QcCommandSyntax::next(QcCommandSyntax::forms(found->usage), parts.words)
                                                             : QList<QcCommandSyntax::Parameter>{};
        for (const QcCommandSyntax::Parameter &parameter : next) {
            words.append(candidates(parameter, parts.typed, sources, found->names));
        }
        prefix = (u"/%1 %2"_s.arg(parts.command, parts.words.join(u' '))).trimmed() + u' ';
    }

    words.removeDuplicates();
    std::ranges::sort(words);
    QStringList lines;
    const QStringList shown = words.mid(0, limit);
    for (const QString &word : shown) {
        lines.append(prefix + word);
    }
    return lines;
}

} // namespace QcAdminCompletion
