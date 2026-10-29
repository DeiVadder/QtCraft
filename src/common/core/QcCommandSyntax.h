// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <optional>
#include <utility>

// The parameters of a command as its usage text names them (decision E-848), for a command picker
// with parameter help and completion. The usage texts of the commands follow one notation: forms
// separated by ", /", " or /" or " | ", plain words as literals, <name> required, [name] optional,
// <a|b|c> a choice, nested groups as in "[<x> <z> [<x2> <z2>]]". The type of a parameter follows
// from its name (player, x/y/z, count, block, item, creature, structure, biome, reason ...). Texts that do not follow
// the notation give one form of plain words; nothing fails.
namespace QcCommandSyntax {

struct Parameter
{
    enum class Kind : quint8 { Literal, Required, Optional };
    // Text takes the rest of the line (a reason, a label); Word any single word; Named a word the
    // server names with its command (a structure, a biome; QcMsg::AdminCatalogue::Command::names,
    // decision E-911).
    enum class Type : quint8 { Word, Text, Player, Integer, Coordinate, Block, Item, Creature, Choice, Named };

    // The literal word, or the placeholder's name as the usage writes it.
    QString name;
    Kind kind = Kind::Literal;
    Type type = Type::Word;
    // The plain words a choice offers ("on", "off"); a choice with a placeholder among its
    // alternatives (<creature>) also has that placeholder's type.
    QStringList choices;
    // Integer ranges written as [0-16].
    std::optional<std::pair<qint64, qint64>> range;

    bool operator==(const Parameter &) const = default;
};

struct Form
{
    QList<Parameter> parameters;

    bool operator==(const Form &) const = default;
};

// The forms of `usage` ("Usage: /tp <x> <y> <z>, /tp <player>"); the leading "Usage:" and the
// command's own name are left out.
QList<Form> forms(const QString &usage);

// The parameters that may follow the words already typed after the command (`words`), one per form
// whose literals and choices match them; empty when no form takes another word.
QList<Parameter> next(const QList<Form> &forms, const QStringList &words);

// A short English name of a type ("player", "integer"), for logs and the control interface.
QLatin1StringView typeName(Parameter::Type type);

} // namespace QcCommandSyntax
