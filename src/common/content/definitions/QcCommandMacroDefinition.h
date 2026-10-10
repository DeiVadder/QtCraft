#pragma once

#include <QString>
#include <QStringList>

// A command made of other commands, defined in data (commands/*.json, decision E-248): mods have no
// C++ access, so this is how they add commands. Each line runs as the caller with the caller's
// rights; $1..$9 take the words after the macro's name, $* all of them. Macros cannot call macros
// and cannot replace the game's commands. Server side only, not synchronised.
struct QcCommandMacroDefinition
{
    static constexpr int maxLines = 16;
    static constexpr int maxLineLength = 256;
    static constexpr int maxTextLength = 256;

    // The command name players type: lower-case letters, digits and '_'.
    QString name;
    // Who may call it at all; every line is checked again for the caller.
    bool operatorOnly = true;
    // Shown by /help as they are (not translated).
    QString usage;
    QString description;
    // Command lines, each starting with '/'.
    QStringList lines;
    // Highest $n the lines use: calls with fewer words answer with the usage.
    int requiredArguments = 0;
};
