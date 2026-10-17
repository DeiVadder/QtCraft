#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// One problem found while loading a texture pack: the file it concerns (relative to the pack, ""
// for the pack itself) and a message. The message is an English source text with %1… placeholders,
// marked for translation in the context "QcTexturePack"; text() translates it for the player.
struct QcTexturePackProblem
{
    QString file;
    const char *message = "";
    QStringList arguments = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free

    // The message in English with the arguments filled in (logs, tests).
    QString englishText() const;
    // The message in the UI language with the arguments filled in, prefixed by the file.
    QString text() const;
};

using QcTexturePackProblems = QList<QcTexturePackProblem>;
