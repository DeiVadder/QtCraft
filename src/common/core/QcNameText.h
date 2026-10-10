#pragma once

#include <QString>

// Names people type and others read (player names, preset names; decision E-232): limits count
// Unicode code points, not UTF-16 units, and cuts fall between graphemes, so an emoji or a letter
// with its accents is kept whole or dropped whole and no lone surrogate is left behind.
namespace QcNameText {

// Code points of `text`; an unpaired surrogate counts as one.
qsizetype codePoints(QStringView text);

// The longest start of `text` that ends on a grapheme boundary and has at most `maxCodePoints`
// code points. Nothing else is changed (no trimming, no filtering).
QString fit(const QString &text, qsizetype maxCodePoints);

// Without unpaired surrogates, non-characters, control and format characters, trimmed, then
// fitted to `maxCodePoints` (and trimmed again).
QString clean(const QString &text, qsizetype maxCodePoints);

} // namespace QcNameText
