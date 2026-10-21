#pragma once

#include <QString>

#include <array>
#include <optional>

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

// UTF-16 units of a colour name on the wire and in a save at most.
inline constexpr qsizetype maxColorUnits = 32;

// The text of one sign: its lines and the colour a dye gave them (decision E-830), the dye's name
// (QcItemDefinition::dye); empty for the default, which is also what black dye gives.
struct Text
{
    Lines lines;
    QString color;

    bool operator==(const Text &) const = default;
};

// The ink of a sign text colour as red, green and blue (decision E-830): a tone of the dye dark
// enough to stand off the lightest planks (birch) at a contrast of at least 2:1.
struct Ink
{
    quint8 red = 0;
    quint8 green = 0;
    quint8 blue = 0;
};

// `lines` as a sign with `count` lines of at most `lineLength` characters keeps them: each line
// cleaned, lines from `count` on empty.
Lines cleaned(const Lines &lines, int count, int lineLength);
bool isEmpty(const Lines &lines);
// No lines and the default colour: nothing to keep for the sign.
bool isBlank(const Text &text);
// The colour a dye named `dye` gives sign text as kept in a Text (black: empty); std::nullopt for
// a name that is no dye colour of signs (the 16 dye colours).
std::optional<QString> colorOfDye(QStringView dye);
// The ink of `color` (a Text's colour); the default ink for empty and unknown names.
Ink inkOf(QStringView color);
// Whether every line fits the wire (maxLineUnits).
bool fitsWire(const Lines &lines);

} // namespace QcSignText
