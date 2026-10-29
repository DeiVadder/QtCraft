// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/entitymeshes/QcEntityMesher.h"
#include "clientcore/entitymeshes/QcEntityVertex.h"
#include "clientcore/light/QcEntityLight.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/world/QcSignText.h"

#include <array>
#include <optional>
#include <vector>

// The text on signs (decision E-766) as quads of the entity mesh: one quad per character, cut from
// the pixel font's pages in the texture atlas (QcPixelFont), on the front of the board in the ink of
// the text's colour (QcSignText::inkOf; dark by default, a dye's tone after decision E-830). Lines
// are centred; a line wider than the board is narrowed to fit. QcSignTextCache keeps the quads of
// each sign until its text, block or light changes.
namespace QcSignTextMesher {

// Font pixels per metre on a sign: 15 wide glyphs fill most of a board of 1 m.
inline constexpr double pixelsPerMetre = 100.0;
// Pixels from one line to the next (7 for the glyphs, 3 between).
inline constexpr int lineStep = 10;
// Width of the text on a board at most, in font pixels.
inline constexpr int maxLineWidth = 94;

// The front of a sign's board: the middle of the text, the direction to the right of a reader,
// and the direction the front faces (up is +y).
struct Face
{
    QcVec3 centre;
    QcVec3 right;
    QcVec3 normal;
};

// The front of the sign block `block` at `pos` (standing: its `rotation` in sixteenths of a turn;
// on a wall: the block it hangs on); std::nullopt for blocks that are no signs.
std::optional<Face> faceOf(const QcContentRegistry &content, QcBlockId block, QcBlockPos pos);

// One quad of the entity mesh.
using Quad = std::array<QcEntityVertex, 4>;

// The quads of the lines of `text` on `face`, in its colour.
[[nodiscard]] std::vector<Quad> textQuads(const Face &face, const QcSignText::Text &text, QcEntityLight light,
                                          const QcEntityMesher::UvLookup &uv);

} // namespace QcSignTextMesher
