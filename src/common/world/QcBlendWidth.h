// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/*! \brief The widths in chunks a ring blending an old into a new generator version may have
    (decision E-1040): a parameter of the conversion, set in the menu or on the command line, not
    a part of the world. */
namespace QcBlendWidth {

/*! The narrowest and widest ring and the width offered first. \qcshared */
inline constexpr int minChunks = 2;
inline constexpr int maxChunks = 16;
inline constexpr int standardChunks = 6;

/*! Returns whether a ring may be \a chunks wide. */
[[nodiscard]] constexpr bool isValid(int chunks)
{
    return chunks >= minChunks && chunks <= maxChunks;
}

} // namespace QcBlendWidth
