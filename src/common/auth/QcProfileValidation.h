// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/mobs/QcAppearance.h"

#include <QString>

#include <optional>

class QcContentRegistry;

// The server's check of a player's profile before it takes it over (Hello, SetAppearance; decisions
// E-036, E-231, E-232): the client stores and sends the profile, but nothing it sends is trusted.
// Outfit, hair and cape names longer than QcAppearance::maxNameLength never get here: the decoder
// refuses the whole message.
namespace QcProfileValidation {

// In Unicode code points (QcNameText).
inline constexpr qsizetype maxPlayerNameLength = 32;

// Only playable outfits, hair styles and capes of the content: an unknown or wrong outfit becomes
// the default outfit (or else any playable one), an unknown or wrong hair style or cape none;
// colours keep their RGB bits.
QcAppearance appearance(const QcContentRegistry &content, const QcAppearance &requested);

// Characters a player name may not hold: shown on other clients, a name must never read as markup.
inline constexpr QStringView markupCharacters = u"<>&";
bool hasMarkup(const QString &name);

// Without unpaired surrogates, control and format characters, trimmed, at most
// maxPlayerNameLength code points cut between graphemes (QcNameText::clean); std::nullopt when no
// letter, digit or symbol is left (empty, only spaces or punctuation) or when it holds one of
// markupCharacters. Commands refuse such names, the login refuses names with markup characters.
[[nodiscard]] std::optional<QString> validPlayerName(const QString &requested);
// The same with "Player" for a name that names nobody (Hello).
QString playerName(const QString &requested);

} // namespace QcProfileValidation
