// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <optional>
#include <utility>

/*! \brief The parameters of a command as its usage text names them (decision E-848), for a command
    picker with parameter help and completion.

    The usage texts of the commands follow one notation: forms separated by ", /", " or /" or " |
    ", plain words as literals, <name> required, [name] optional, <a|b|c> a choice, nested groups
    as in "[<x> <z> [<x2> <z2>]]". The type of a parameter follows from its name (player, x/y/z,
    count, block, item, creature, structure, biome, reason ...). Texts that do not follow the
    notation give one form of plain words; nothing fails. */
namespace QcCommandSyntax {

/*! \brief One word of a form: a literal or a placeholder. */
struct Parameter
{
    /*! How the word stands in the usage. */
    enum class Kind : quint8 {
        /*! A plain word, typed as it is. */
        Literal,
        /*! A word that must be given: a placeholder in <...> or plain alternatives a|b. */
        Required,
        /*! A word that may be left out: anything inside [...]. */
        Optional
    };
    /*! What a placeholder takes, from its name. */
    enum class Type : quint8 {
        /*! Any single word. */
        Word,
        /*! The rest of the line (a reason, a label). */
        Text,
        /*! A player's name. */
        Player,
        /*! A whole number (count, seconds, level, a range like 0-16 ...). */
        Integer,
        /*! A coordinate (x, y, z, x2 ...). */
        Coordinate,
        /*! A block. */
        Block,
        /*! An item. */
        Item,
        /*! A creature. */
        Creature,
        /*! One of the alternatives in \c {choices}. */
        Choice,
        /*! A word the server names with its command (a structure, a biome;
            QcMsg::AdminCatalogue::Command::names, decision E-911). */
        Named
    };

    /*! The literal word, or the placeholder's name as the usage writes it. */
    QString name;
    /*! Literal, required or optional. */
    Kind kind = Kind::Literal;
    /*! What a placeholder takes; Word for a literal. */
    Type type = Type::Word;
    /*! The plain words a choice offers ("on", "off"); a choice with a placeholder among its
        alternatives (<creature>) also has that placeholder's type. */
    QStringList choices;
    /*! Integer ranges written as [0-16]. */
    std::optional<std::pair<qint64, qint64>> range;

    bool operator==(const Parameter &) const = default;
};

/*! \brief One form of a command's usage. */
struct Form
{
    /*! The words after the command's name, in order. */
    QList<Parameter> parameters;

    bool operator==(const Form &) const = default;
};

/*! Returns the forms of \a usage ("Usage: /tp <x> <y> <z>, /tp <player>"); the leading "Usage:"
    and the command's own name are left out. */
QList<Form> forms(const QString &usage);

/*! Returns the parameters of \a forms that may follow the words already typed after the command
    (\a words), one per form whose literals and choices match them; empty when no form takes
    another word. */
QList<Parameter> next(const QList<Form> &forms, const QStringList &words);

/*! Returns a short English name of \a type ("player", "integer"), for logs and the control
    interface. */
QLatin1StringView typeName(Parameter::Type type);

} // namespace QcCommandSyntax
