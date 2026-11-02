// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

/*! \brief One problem found while loading a texture pack: the file it concerns (relative to the
    pack, "" for the pack itself) and a message.

    The message is an English source text with %1… placeholders, marked for translation in the
    context "QcTexturePack"; text() translates it for the player. \qcfields */
struct QcTexturePackProblem
{
    QString file;
    const char *message = "";
    QStringList arguments = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free

    /*! Returns the message in English with the arguments filled in (logs, tests). */
    QString englishText() const;
    /*! Returns the message in the UI language with the arguments filled in, prefixed by the
        file. */
    QString text() const;
};

/*! Problems of a texture pack, in the order they were found. */
using QcTexturePackProblems = QList<QcTexturePackProblem>;
