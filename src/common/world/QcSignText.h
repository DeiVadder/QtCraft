#pragma once

#include <QString>

#include <array>

// The text on a sign (decision E-766): up to four lines that a player writes and everyone reads.
// Lines are cleaned like names (QcNameText): no control or format characters, trimmed, at most a
// block's QcSignDefinition::lineLength characters (code points, cut between graphemes).
namespace QcSignText {

inline constexpr int maxLines = 4;
// Characters of a line at most in any block's data.
inline constexpr int maxLineLength = 32;
// UTF-16 units of a line on the wire and in a save at most (before cleaning).
inline constexpr qsizetype maxLineUnits = 256;

using Lines = std::array<QString, maxLines>;

// `lines` as a sign with `count` lines of at most `lineLength` characters keeps them: each line
// cleaned, lines from `count` on empty.
Lines cleaned(const Lines &lines, int count, int lineLength);
bool isEmpty(const Lines &lines);
// Whether every line fits the wire (maxLineUnits).
bool fitsWire(const Lines &lines);

} // namespace QcSignText
