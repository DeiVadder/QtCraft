// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/light/QcChunkLight.h"
#include "clientcore/meshing/QcBlockRenderTable.h"
#include "common/core/QcBlockPos.h"
#include "common/light/QcLightEngine.h"
#include "common/world/QcChunk.h"

#include <QList>
#include <QSet>

#include <memory>
#include <unordered_map>

// Block light of all loaded chunks on the client (decision E-049): the shared QcLightEngine (decision
// E-389) with block light only, plus immutable section snapshots for the mesher. The engine keeps
// the light up to date incrementally; takeChanges() publishes the sections whose levels differ from
// their last snapshot and names the sections whose meshes read a changed cell. Sky light stays in
// the mesher. Not thread-safe: one thread uses it.
class QcBlockLightEngine
{
public:
    struct SectionRef
    {
        QcChunkPos chunk;
        int section = 0;
    };
    struct SectionUpdate
    {
        QcChunkPos chunk;
        int section = 0;
        // Null when the section is dark.
        std::shared_ptr<const QcChunkLight::Section> light;
    };
    // What changed since the last takeChanges(): new snapshots of sections whose levels differ,
    // and the sections whose meshes read a changed cell (also neighbours of changed border cells).
    struct Changes
    {
        QList<SectionUpdate> sections;
        QList<SectionRef> remesh;
    };

    explicit QcBlockLightEngine(const std::shared_ptr<const QcBlockRenderTable> &table);

    void addChunk(std::shared_ptr<const QcChunk> chunk);
    void removeChunk(QcChunkPos pos);
    // `chunk` is the snapshot of the chunk at `pos` after the change.
    void changeBlock(QcBlockPos pos, std::shared_ptr<const QcChunk> chunk);
    Changes takeChanges();

    bool hasChunk(QcChunkPos pos) const { return m_engine.hasChunk(pos); }
    // The published light of a chunk (as of the last takeChanges()).
    QcChunkLight snapshot(QcChunkPos pos) const;
    // Current level; 0 in unloaded chunks.
    int level(QcBlockPos pos) const { return m_engine.blockLight(pos); }
    // Cells visited by the propagation steps so far (work measure for benchmarks).
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
