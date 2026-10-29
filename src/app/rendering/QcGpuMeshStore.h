// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/rendering/QcDrawOrigins.h"
#include "app/rendering/QcSectionArena.h"
#include "app/rendering/QcSectionLightPool.h"
#include "clientcore/meshing/QcChunkMesh.h"
#include "clientcore/meshing/QcEntityMesh.h"
#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"

#include <QList>
#include <QVector3D>
#include <rhi/qrhi.h>

#include <memory>
#include <unordered_map>
#include <utility>

// One list of indexed triangles in a vertex and an index buffer: where it starts in each (bytes),
// how many indices it has and their size. The buffers belong to whoever holds the list. The same
// start as the first index and the base vertex of a draw from the start of both buffers, so lists
// sharing the buffers are drawn with one binding where QRhi::BaseVertex is supported (decision E-820).
struct QcGpuList
{
    QRhiBuffer *vertices = nullptr;
    QRhiBuffer *indices = nullptr;
    quint32 vertexOffset = 0;
    quint32 indexOffset = 0;
    quint32 indexCount = 0;
    QRhiCommandBuffer::IndexFormat indexFormat = QRhiCommandBuffer::IndexUInt32;
    // indexOffset in indices of indexFormat, vertexOffset in vertices.
    quint32 firstIndex = 0;
    qint32 baseVertex = 0;
};

// A vertex and an index buffer on the GPU with what a draw needs from them.
struct QcGpuBuffers
{
    std::unique_ptr<QRhiBuffer> vertices;
    std::unique_ptr<QRhiBuffer> indices;
    quint32 indexCount = 0;
    QRhiCommandBuffer::IndexFormat indexFormat = QRhiCommandBuffer::IndexUInt32;
    // The uploaded mesh's origin (QcEntityMesh::origin), the vertices being relative to it.
    QcVec3 origin;
    quint64 bytes() const { return (vertices ? vertices->size() : 0) + (indices ? indices->size() : 0); }
    QcGpuList list() const { return {vertices.get(), indices.get(), 0, 0, indexCount, indexFormat}; }

    // Uploads an entity mesh into dynamic buffers that grow on demand and are reused every frame;
    // nothing to draw without a mesh. The bytes uploaded.
    [[nodiscard]] quint32 uploadDynamic(QRhi *rhi, QRhiResourceUpdateBatch *updates, const QcEntityMesh *mesh);
};

// The lists of one section on the GPU, packed together (QcPackedMesh, decision E-813) into one slot
// of a page of the section arena (decision E-820).
struct QcGpuMesh
{
    QcArenaSlot slot;
    QcGpuList opaque;
    // Cut-out faces of the opaque pass (alpha-tested, QcChunkMesh).
    QcGpuList cutout;
    QcGpuList translucent;
    QcGpuList water;
    // Faces towards the void below the world, drawn only from below y = 0 (QcChunkMesh).
    QcGpuList underside;
    // Water and other translucent faces in the mesher's order (QcChunkMesh::translucentRuns).
    QList<quint32> translucentRuns;
    // The section's corner in the world (QcChunkMesh::origin) and the box of its vertices relative
    // to it (decision E-883).
    QcBlockPos origin;
    QVector3D boundsMin;
    QVector3D boundsMax;
    // The entry of the corner in the store's draw origins (QcDrawOrigins); none without faces.
    quint32 drawOrigin = QcDrawOrigins::none;
    // GPU bytes of the section's data in its page.
    quint64 bytes() const { return slot.bytes(); }
    // The middle of the vertices' box relative to the corner.
    QVector3D middle() const { return (boundsMin + boundsMax) * 0.5f; }
    // Whether a frame draws anything of the section: any list with faces; the underside only from
    // below the world (a section of plants or leaves alone has only cut-out faces, decision E-756).
    bool isDrawable(bool belowWorld) const
    {
        return opaque.indexCount > 0 || cutout.indexCount > 0 || translucent.indexCount > 0 || water.indexCount > 0
               || (belowWorld && underside.indexCount > 0);
    }
};

// The sections of the world on the GPU, one mesh per section key in the pages of a section arena
// (decision E-820): the meshes received since the last frame are queued and uploaded in one resource
// update batch, together with their per-cell light when the frame plan reads it (QcSectionLightPool).
// Each section with faces holds an entry of the draw origins (decision E-883) from its upload to its
// removal; the renderer adds the entries of its other draws to the same origins. Drawing them is
// QcWorldRenderer's part. Render thread only.
class QcGpuMeshStore
{
public:
    using Meshes = std::unordered_map<quint64, QcGpuMesh>;

    // Forgets every section and draw origin, for a new QRhi; `sectionLight` says whether the plan
    // reads the light pool.
    void reset(QRhi *rhi, bool sectionLight);
    // Removes every section with the next upload, together with what was queued before.
    void clearAll()
    {
        m_clearAll = true;
        m_uploads.clear();
    }
    // The section's latest mesh (null: removed); a section queued twice keeps only the later mesh.
    void queue(quint64 key, std::shared_ptr<const QcChunkMesh> mesh) { m_uploads.insert_or_assign(key, std::move(mesh)); }
    // Uploads the queued meshes into `updates`; returns the bytes uploaded.
    quint64 uploadQueued(QRhiResourceUpdateBatch *updates);

    const Meshes &meshes() const { return m_meshes; }
    // The origins of the draws, the sections' and the renderer's other ones.
    QcDrawOrigins &origins() { return m_origins; }
    const QcDrawOrigins &origins() const { return m_origins; }
    // GPU bytes of the section pages (their capacity, decision E-820).
    quint64 bytes() const { return m_arena.bytes(); }
    // Pages of the section arena.
    int pageCount() const { return m_arena.pageCount(); }
    // Shared pages where sections are written directly, otherwise a page per section.
    QcSectionArena::Layout layout() const { return m_arena.layout(); }
    // Section buffers written directly (QcBufferWrite) since the last reset().
    quint64 directWrites() const { return m_directWrites; }

private:
    // The section's lists in one slot of the arena; empty without faces.
    QcGpuMesh createMesh(QRhiResourceUpdateBatch *updates, const QcChunkMesh &mesh);
    // Writes `data` to `offset` in a section page: directly where possible, otherwise through `updates`.
    void fill(QRhiResourceUpdateBatch *updates, QRhiBuffer *buffer, quint32 offset, const QByteArray &data);
    // The light of a received section into the pool, when the plan's steps read it.
    void uploadSectionLight(QRhiResourceUpdateBatch *updates, quint64 key, const QcChunkMesh *mesh);
    void release(quint64 key);

    QRhi *m_rhi = nullptr;
    bool m_sectionLight = false;
    // Section pages are written directly (QcBufferWrite) and created with the storage usage.
    bool m_directWrite = false;
    // A failed direct write was reported (once per QRhi).
    bool m_warnedWriteFallback = false;
    quint64 m_directWrites = 0;
    Meshes m_meshes;
    QcDrawOrigins m_origins;
    QcSectionArena m_arena;
    // The latest mesh of each section changed since the last frame (null: removed). One entry per
    // section: only the latest mesh is worth a slot and an upload (decision E-413).
    std::unordered_map<quint64, std::shared_ptr<const QcChunkMesh>> m_uploads;
    bool m_clearAll = false;
    // Per-cell light of the sections, filled only when m_sectionLight is set.
    QcSectionLightPool m_lightPool;
};
