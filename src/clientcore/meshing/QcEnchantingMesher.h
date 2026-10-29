// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/light/QcEntityLight.h"
#include "clientcore/meshing/QcEntityMesh.h"
#include "clientcore/meshing/QcEntityMesher.h"
#include "clientcore/session/QcEnchantingAura.h"

#include <array>

// The enchanting tables' book and runes as parts of the entity mesh (decision E-884, positions from
// QcEnchantingAura): the book as two quads of its atlas tile (QcRuneGlyphs::bookTile), the two halves
// hinged at the spine and opened towards the viewer; each rune as a quad cut from the rune pages of the texture atlas
// (QcRuneGlyphs), facing the viewer about the vertical and glowing in a pale violet.
namespace QcEnchantingMesher {

// The tint of the runes, which are white in the atlas.
inline constexpr std::array<quint8, 4> runeTint = {214, 196, 255, 255};

// The book's two halves, drawn from both sides.
void appendBook(QcEntityMesh &mesh, const QcEnchantingAura::Book &book, QcEntityLight light, const QcEntityMesher::UvLookup &uv);
// A rune facing `viewer`, drawn from both sides.
void appendRune(QcEntityMesh &mesh, const QcEnchantingAura::Rune &rune, const QcVec3 &viewer, const QcEntityMesher::UvLookup &uv);

} // namespace QcEnchantingMesher
