// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"

#include <QList>
#include <QString>

/*! \brief A text floating in the world (showcase names, decision E-222).

    The server sends the labels of a chunk with it (QcMsg::ChunkLabels); the client translates
    \c {text} with the content's translations (an untranslated key is shown as it is), adds the
    plain \c {detail} line and draws the labels near the player when labels are switched on. */
struct QcWorldLabel
{
    /*! The longest text and detail line (UTF-16 units) the protocol accepts. */
    static constexpr qsizetype maxTextLength = 256;
    /*! The most labels of one chunk the protocol accepts. */
    static constexpr qsizetype maxPerChunk = 4096;

    /*! Returns \a text cut to at most maxTextLength UTF-16 units on a character boundary, ending
        in "…" when it was cut, so that the protocol accepts it. */
    static QString limitedText(const QString &text);
    /*! Returns the label with text and detail line limited like that. */
    QcWorldLabel limited() const;

    /*! Where it floats. */
    QcVec3 position;
    /*! The text or its translation key. */
    QString text;
    /*! A plain line below it; may be empty. */
    QString detail;
    /*! Titles of areas: larger and seen from further away. */
    bool heading = false;

    bool operator==(const QcWorldLabel &) const = default;
};
