// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QtGlobal>

#include <vector>

/*! \brief How the enchanting screen writes an offer's clue (decision E-884): every letter is either
    readable or hidden behind a rune (QcRuneGlyphs), and the bookshelves around the table decide how
    many are readable: floor(letters x shelves / 15), all of them at 15.

    Which letters come first follows from the rune seed the server sends with the offers
    (EnchantingState::runeSeed, drawn apart from the player's enchanting seed) and the offer's
    slot alone, so a letter, once readable, stays readable when more shelves are added, nothing
    flickers while the screen is open, and the order changes only after the player enchants. A
    hidden letter's rune follows from the seed and the letter (a substitution cipher for the
    seed: the same letter shows the same rune in all three offers). Spaces stay gaps. */
namespace QcRuneScript {

/*! Bookshelves at which every letter is readable. */
inline constexpr int fullShelves = 15;

/*! \brief One character of a clue as the screen draws it: a rune (its index), or the readable
    character (rune -1).

    A space is readable. */
struct Sign
{
    /*! The rune's index (QcRuneGlyphs); -1 for a readable character. */
    int rune = -1;
    /*! The readable character; empty for a rune. */
    QString character;

    bool operator==(const Sign &) const = default;
};

/*! Returns how many of \a letters are readable with \a shelves bookshelves (0..15, clamped). */
[[nodiscard]] int readableCount(int letters, int shelves);
/*! Returns the clue \a text of offer \a slot as runes and readable characters, with the rune
    seed \a seed and \a shelves bookshelves. */
[[nodiscard]] std::vector<Sign> write(const QString &text, quint32 seed, int slot, int shelves);
/*! \brief How much of a clue a reader can make out: its letters (no spaces) and how many of them
    are readable. */
struct Legibility
{
    /*! Letters of the clue, spaces not counted. */
    int letters = 0;
    /*! How many of them are readable. */
    int readable = 0;

    bool operator==(const Legibility &) const = default;
};
/*! Returns how many letters of \a signs there are and how many are readable (what screen readers
    say: "n of m letters readable", or the clue when all are). */
[[nodiscard]] Legibility legibility(const std::vector<Sign> &signs);

} // namespace QcRuneScript
