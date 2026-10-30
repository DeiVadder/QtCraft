// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

/*! \brief A command made of other commands, defined in data (commands/\unicode {0x2A}.json,
    decision E-248): mods have no C++ access, so this is how they add commands.

    Each line runs as the caller with the caller's rights; $1..$9 take the words after the
    macro's name, $* all of them. Macros cannot call macros and cannot replace the game's
    commands. Server side only, not synchronised. */
struct QcCommandMacroDefinition
{
    /*! The most command lines a macro may run. */
    static constexpr int maxLines = 16;
    /*! The longest command line, in characters. */
    static constexpr int maxLineLength = 256;
    /*! The length, in characters, \c {usage} and \c {description} are cut to. */
    static constexpr int maxTextLength = 256;

    /*! The command name players type: lower-case letters, digits and '_'. */
    QString name;
    /*! Who may call it at all (JSON \c {"permission"}: "operator", the default, or "player");
        every line is checked again for the caller. */
    bool operatorOnly = true;
    /*! The usage and the description shown by /help as they are (not translated); the usage is
        "/<name>" by default. \qcshared */
    QString usage;
    QString description;
    /*! Command lines (JSON \c {"run"}), each starting with '/'. */
    QStringList lines;
    /*! Highest $n the lines use: calls with fewer words answer with the usage. */
    int requiredArguments = 0;
};
