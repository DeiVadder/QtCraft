// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/*! \brief The versions of the world generator (decision E-922).

    A world keeps the version it was created with in world.json ("generator"/"version") and is
    generated with it for good, so that chunks made after an update fit those made before. A
    version promises the output of the base game's generator code together with the base game's
    worldgen data at that version (terrain, flat and void worlds; the showcase is a demonstration
    world that always generates with the current code); what mods add on top is the player's
    choice. tests/terrain/test_generator_versions checks every version against recorded checksums
    of generated chunks; a change of the generator's output needs a new version (checklist in
    docs/Entscheidungen.md, E-922).

    The numbers count per dimension (decision E-979): the constants below are those of the
    overworld; a dimension follows the Series of the generator it uses
    (QcWorldGenerators::versionSeries), and world.json keeps a version per dimension. */
namespace QcGeneratorVersion {

/*! The version of worlds saved without one (before decision E-922). */
inline constexpr int unversioned = 1;
/*! The oldest version this game still generates. */
inline constexpr int oldest = 1;
/*! The newest version this game generates: the version of new worlds. */
inline constexpr int current = 3;
/*! The newest frozen version, 0 for none: a version freezes when a release build ships it, and
    its output is kept for good from then on. A newer one is open: it may still change (its
    checksums are recorded anew) until a release ships it (decision E-922). Set to \c {current}
    with each release; release 0.1.0 froze version 1, release 1.0.0 version 2, release 1.1.0 version 3. */
inline constexpr int lastFrozen = 3;
/*! Highest number world.json may name (a newer game's version) before it counts as damaged. */
inline constexpr int maxStored = 1000;

/*! Returns whether this game generates \a version (oldest to current). */
[[nodiscard]] constexpr bool isKnown(int version)
{
    return version >= oldest && version <= current;
}

/*! \brief The versions of one line of generators (decision E-979): the oldest one this game
    still generates, the newest one and the newest frozen one (0 for none), as for the overworld
    above. \qcfields */
struct Series
{
    int oldest = 1;
    int current = 1;
    int lastFrozen = 0;
    /*! The name of the series: a number means something only within its series, so world.json
        keeps the name beside the number of a dimension of another series than the overworld's
        (decision E-1033). */
    const char *name = "overworld";

    /*! Returns whether this game generates \a version of the series. */
    [[nodiscard]] constexpr bool isKnown(int version) const { return version >= oldest && version <= current; }
    /*! Returns the version a dimension gets in a world that has none for it yet (a world older
        than the dimension, or a dimension a mod added later): the newest frozen one, the open
        one while none is frozen. New worlds give every dimension \c {current}. */
    [[nodiscard]] constexpr int forLateDimension() const { return lastFrozen >= oldest ? lastFrozen : current; }
};

/*! The versions of the overworld's generators (terrain, flat, void, showcase). */
inline constexpr Series overworld{oldest, current, lastFrozen};
/*! The versions of the nether generator (decision E-1033): version 1 is frozen since release
    1.1.0. */
inline constexpr Series nether{1, 1, 1, "nether"};

} // namespace QcGeneratorVersion
