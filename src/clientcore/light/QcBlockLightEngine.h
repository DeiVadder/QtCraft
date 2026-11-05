// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/light/QcChunkLight.h"
#include "clientcore/meshing/QcBlockRenderTable.h"
#include "common/core/QcBlockPos.h"
#include "common/core/QcSectionKey.h"
#include "common/light/QcLightEngine.h"
#include "common/world/QcChunk.h"

#include <QList>
#include <QSet>

#include <memory>
#include <unordered_map>

/*! \brief Block light of all loaded chunks on the client (decision E-049): the shared QcLightEngine
    (decision E-389) with block light only, plus immutable section snapshots for the mesher.

    The engine keeps the light up to date incrementally; takeChanges() publishes the sections
    whose levels differ from their last snapshot and names the sections whose meshes read a
    changed cell. Sky light stays in the mesher. Not thread-safe: one thread uses it. */
class QcBlockLightEngine
{
public:
    /*! \brief A new snapshot of one section's light. */
    struct SectionUpdate
    {
        /*! Position of the chunk. */
        QcChunkPos chunk;
        /*! Index of the section within the chunk (0 at the bottom). */
        int section = 0;
        /*! Null when the section is dark. */
        std::shared_ptr<const QcChunkLight::Section> light;
    };
    /*! \brief What changed since the last takeChanges(): new snapshots of sections whose levels
        differ, and the sections whose meshes read a changed cell (also neighbours of changed
        border cells). */
    struct Changes
    {
        /*! New snapshots of the sections whose levels differ. */
        QList<SectionUpdate> sections;
        /*! Sections whose meshes read a changed cell. */
        QList<QcSectionAddress> remesh;
    };

    explicit QcBlockLightEngine(const std::shared_ptr<const QcBlockRenderTable> &table);

    /*! Adds the snapshot \a chunk and spreads the light of its sources and of its neighbours into
        it. */
    void addChunk(std::shared_ptr<const QcChunk> chunk);
    /*! Removes the chunk at \a pos and the light it gave its neighbours. */
    void removeChunk(QcChunkPos pos);
    /*! Updates the light after the block at \a pos changed; \a chunk is the snapshot after
        the change. */
    void changeBlock(QcBlockPos pos, std::shared_ptr<const QcChunk> chunk);
    /*! Returns what changed since the last call and starts over. */
    Changes takeChanges();

    /*! Returns whether the chunk at \a pos is loaded. */
    bool hasChunk(QcChunkPos pos) const { return m_engine.hasChunk(pos); }
    /*! Returns the published light of the chunk at \a pos (as of the last takeChanges()). */
    QcChunkLight snapshot(QcChunkPos pos) const;
    /*! Returns the current level at \a pos; 0 in unloaded chunks. */
    int level(QcBlockPos pos) const { return m_engine.blockLight(pos); }
    /*! Returns the cells visited by the propagation steps so far (work measure for benchmarks). */
    quint64 visitedCells() const { return m_engine.visitedCells(); }

private:
    struct PosHash
    {
        std::size_t operator()(QcChunkPos pos) const { return static_cast<std::size_t>(qHash(pos)); }
    };
    using Published = std::array<std::shared_ptr<const QcChunkLight::Section>, QcWorld::sectionCount>;

    // Snapshot of the engine's current levels of a section; null when dark.
    std::shared_ptr<const QcChunkLight::Section> current(QcChunkPos pos, int section) const;

    QcLightEngine m_engine;
    std::unordered_map<QcChunkPos, Published, PosHash> m_published;
};
