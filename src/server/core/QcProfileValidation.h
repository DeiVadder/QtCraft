#pragma once

#include "QcAppearance.h"

#include <QString>

class QcContentRegistry;

// The server's check of a player's profile before it takes it over (Hello, SetAppearance; decisions
// E-036, E-231, E-232): the client stores and sends the profile, but nothing it sends is trusted.
// Outfit and hair names longer than QcAppearance::maxNameLength never get here: the decoder
// refuses the whole message.
namespace QcProfileValidation {

// In Unicode code points (QcNameText).
inline constexpr qsizetype maxPlayerNameLength = 32;

// Only playable outfits and hair styles of the content: an unknown or wrong outfit becomes the
// default outfit (or else any playable one), an unknown or wrong hair style none; colours keep
// their RGB bits.
QcAppearance appearance(const QcContentRegistry &content, const QcAppearance &requested);

// Without unpaired surrogates, control and format characters, trimmed, at most
// maxPlayerNameLength code points cut between graphemes (QcNameText::clean); "Player" when
// nothing is left.
QString playerName(const QString &requested);

} // namespace QcProfileValidation
