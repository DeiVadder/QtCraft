// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"

#include <optional>

class QcContentRegistry;
class QcNetherGenerator;

/*! \brief The spawn search of the nether (decision E-1111): on the blocks the generator really
    places, not on the bare rock.

    Columns on rings of chunks around the origin, each chunk generated once and looked at in
    five columns: the lowest cell there that QcSafeStand::fits (firm ground, two free cells, no
    lava, fire or magma near) is the spawn. First only in the biomes of
    QcNetherSettings::spawnBiomes within preferredRings, then in every biome within rings; chunks
    with residents (a bastion's) are left out. Without any fitting cell the first floor above the
    lava on the bare rock, as before. */
class QcNetherSpawn
{
public:
    /*! The rings of chunks the search with a preferred biome looks at. */
    static constexpr int preferredRings = 12;
    /*! The rings of chunks the search looks at in all. */
    static constexpr int rings = 32;

    /*! Builds the search over the chunks of \a generator, whose blocks are those of \a content. */
    QcNetherSpawn(const QcNetherGenerator &generator, const QcContentRegistry &content);

    /*! Returns the feet of the spawn. */
    [[nodiscard]] QcBlockPos find() const;

private:
    // The first fitting cell on the rings up to `count`; with `preferred` only in its biomes.
    [[nodiscard]] std::optional<QcBlockPos> onRings(int count, bool preferred) const;
    // The lowest fitting cell of the chunk at chunk coordinates `chunkX`, `chunkZ`.
    [[nodiscard]] std::optional<QcBlockPos> inChunk(int chunkX, int chunkZ, bool preferred) const;
    // Whether the column's biome is one the search looks in first.
    [[nodiscard]] bool preferredAt(int x, int z) const;
    // The first floor above the lava on the bare rock, on the same rings.
    [[nodiscard]] QcBlockPos onRock() const;

    const QcNetherGenerator &m_generator;
    const QcContentRegistry &m_content;
};
