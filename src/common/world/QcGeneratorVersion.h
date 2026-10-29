// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The versions of the world generator (decision E-922). A world keeps the version it was created
// with in world.json ("generator"/"version") and is generated with it for good, so that chunks made
// after an update fit those made before. A version promises the output of the base game's generator
// code together with the base game's worldgen data at that version (terrain, flat and void worlds;
// the showcase is a demonstration world that always generates with the current code); what mods add
// on top is the player's choice. tests/terrain/test_generator_versions checks every version against
// recorded checksums of generated chunks; a change of the generator's output needs a new version
// (checklist in docs/Entscheidungen.md, E-922).
namespace QcGeneratorVersion {

// The version of worlds saved without one (before decision E-922).
inline constexpr int unversioned = 1;
// The oldest version this game still generates, and the version of new worlds.
inline constexpr int oldest = 1;
inline constexpr int current = 1;
// The newest frozen version, 0 for none: a version freezes when a release build ships it, and its
// output is kept for good from then on. A newer one is open: it may still change (its checksums are
// recorded anew) until a release ships it (decision E-922). Set to `current` with each release.
inline constexpr int lastFrozen = 0;
// Highest number world.json may name (a newer game's version) before it counts as damaged.
inline constexpr int maxStored = 1000;

[[nodiscard]] constexpr bool isKnown(int version)
{
    return version >= oldest && version <= current;
}

} // namespace QcGeneratorVersion
