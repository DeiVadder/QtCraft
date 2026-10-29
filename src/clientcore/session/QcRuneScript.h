// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QtGlobal>

#include <vector>

// How the enchanting screen writes an offer's clue (decision E-884): every letter is either readable
// or hidden behind a rune (QcRuneGlyphs), and the bookshelves around the table decide how many are
// readable: floor(letters x shelves / 15), all of them at 15. Which letters come first follows from
// the rune seed the server sends with the offers (EnchantingState::runeSeed, drawn apart from the
// player's enchanting seed) and the offer's slot alone, so a letter, once readable, stays readable
// when more shelves are added, nothing flickers while the screen is open, and the order changes only
// after the player enchants. A hidden letter's rune follows from the seed and the letter (a substitution
// cipher for the seed: the same letter shows the same rune in all three offers). Spaces stay gaps.
namespace QcRuneScript {

inline constexpr int fullShelves = 15;

// One character of a clue as the screen draws it: a rune (its index), or the readable character
// (rune -1). A space is readable.
struct Sign
{
    int rune = -1;
    QString character;

    bool operator==(const Sign &) const = default;
};

// Letters of `letters` that are readable with `shelves` bookshelves (0..15, clamped).
[[nodiscard]] int readableCount(int letters, int shelves);
// The clue `text` of offer `slot` as runes and readable characters.
[[nodiscard]] std::vector<Sign> write(const QString &text, quint32 seed, int slot, int shelves);
// How much of a clue a reader can make out: its letters (no spaces) and how many of them are readable.
struct Legibility
{
    int letters = 0;
    int readable = 0;

    bool operator==(const Legibility &) const = default;
};
// What screen readers say of the signs: "n of m letters readable", or the clue when all are.
[[nodiscard]] Legibility legibility(const std::vector<Sign> &signs);

} // namespace QcRuneScript
