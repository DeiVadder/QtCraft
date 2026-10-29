// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

#include <optional>

// One pixel of a map (decision E-799): the top block of its column as the index of its block
// definition (QcBlockState::type, the same for all states of a block, an index into
// QcContentRegistry::blocks()) and a shade, in 16 bits as (block + 1) << 2 | shade; 0 is a pixel
// nobody has looked at yet. The client finds the colour of the block itself (decision E-801), so
// texture packs colour maps too. Saves name the blocks (QcMapCodec); the indices never leave a
// running world.
namespace QcMapPixel {

// How the pixel is shaded (decision E-800): darker where the land climbs towards the north or the
// water is deep, brighter where it falls.
enum class Shade : quint8 {
    Dark,
    Normal,
    Bright,
};
inline constexpr int shadeCount = 3;

// Brightness of each shade, in 255ths (as the reference: 180, 220, 255).
inline constexpr int brightness[shadeCount] = {180, 220, 255};

inline constexpr quint16 blank = 0;
// The largest block index a pixel can name; larger ones (very large mods) leave their pixels blank.
inline constexpr quint32 maxBlock = (0xffffu >> 2) - 1;

[[nodiscard]] constexpr quint16 make(quint32 block, Shade shade)
{
    return block > maxBlock ? blank : static_cast<quint16>(((block + 1) << 2) | static_cast<quint16>(shade));
}

// The block of the pixel, std::nullopt for a blank pixel.
[[nodiscard]] constexpr std::optional<quint32> blockOf(quint16 pixel)
{
    return pixel >> 2 == 0 ? std::nullopt : std::optional<quint32>((pixel >> 2) - 1);
}

// Shade bits outside the three shades read as Normal.
[[nodiscard]] constexpr Shade shadeOf(quint16 pixel)
{
    const int bits = pixel & 3;
    return bits < shadeCount ? static_cast<Shade>(bits) : Shade::Normal;
}

} // namespace QcMapPixel
