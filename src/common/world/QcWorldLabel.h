#pragma once

#include "common/core/QcVec3.h"

#include <QList>
#include <QString>

// A text floating in the world (showcase names, decision E-222). The server sends the labels of a
// chunk with it (QcMsg::ChunkLabels); the client translates `text` with the content's translations
// (an untranslated key is shown as it is), adds the plain `detail` line and draws the labels near
// the player when labels are switched on.
struct QcWorldLabel
{
    static constexpr qsizetype maxTextLength = 256;
    static constexpr qsizetype maxPerChunk = 4096;

    // `text` cut to at most maxTextLength UTF-16 units on a character boundary, ending in "…" when
    // it was cut, so that the protocol accepts it.
    static QString limitedText(const QString &text);
    // The label with text and detail line limited like that.
    QcWorldLabel limited() const;

    QcVec3 position;
    QString text;
    QString detail;
    // Titles of areas: larger and seen from further away.
    bool heading = false;

    bool operator==(const QcWorldLabel &) const = default;
};
