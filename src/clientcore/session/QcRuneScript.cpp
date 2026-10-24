#include "clientcore/session/QcRuneScript.h"

#include "clientcore/textures/QcRuneGlyphs.h"

#include <QList>

#include <algorithm>
#include <utility>

namespace {

// A well mixed 64-bit value of `value` (splitmix64's finaliser).
quint64 mix(quint64 value)
{
    value += 0x9E3779B97F4A7C15ull;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31);
}

// Where letter `position` of offer `slot` stands in the order the letters become readable.
quint64 rankOf(quint32 seed, int slot, int position)
{
    return mix(mix(quint64{seed} * 3 + static_cast<quint64>(slot)) ^ static_cast<quint64>(position));
}

// The rune that hides `character` for `seed`.
int runeOf(quint32 seed, char32_t character)
{
    return static_cast<int>(mix((quint64{seed} << 21) ^ character) % QcRuneGlyphs::count);
}

} // namespace

namespace QcRuneScript {

int readableCount(int letters, int shelves)
{
    return std::max(0, letters) * std::clamp(shelves, 0, fullShelves) / fullShelves;
}

std::vector<Sign> write(const QString &text, quint32 seed, int slot, int shelves)
{
    const QList<uint> characters = text.toUcs4();
    std::vector<Sign> signs;
    signs.reserve(static_cast<std::size_t>(characters.size()));
    // The positions of the letters, in the order they become readable.
    std::vector<int> letters;
    for (int position = 0; std::cmp_less(position, characters.size()); ++position) {
        const auto character = static_cast<char32_t>(characters[position]);
        signs.push_back({-1, QString::fromUcs4(&character, 1)});
        if (!QChar::isSpace(character)) {
            letters.push_back(position);
        }
    }
    std::ranges::sort(letters, {}, [seed, slot](int position) { return rankOf(seed, slot, position); });

    const int readable = readableCount(static_cast<int>(letters.size()), shelves);
    for (std::size_t index = static_cast<std::size_t>(readable); index < letters.size(); ++index) {
        Sign &sign = signs[static_cast<std::size_t>(letters[index])];
        sign.rune = runeOf(seed, static_cast<char32_t>(characters[letters[index]]));
    }
    return signs;
}

Legibility legibility(const std::vector<Sign> &signs)
{
    Legibility counted;
    for (const Sign &sign : signs) {
        if (sign.rune >= 0) {
            ++counted.letters;
        } else if (!sign.character.trimmed().isEmpty()) {
            ++counted.letters;
            ++counted.readable;
        }
    }
    return counted;
}

} // namespace QcRuneScript
