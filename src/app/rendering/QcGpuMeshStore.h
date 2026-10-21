#pragma once

#include "app/rendering/QcSectionLightPool.h"
#include "clientcore/meshing/QcChunkMesh.h"
#include "clientcore/meshing/QcEntityMesh.h"

#include <QList>
#include <QVector3D>
#include <rhi/qrhi.h>

#include <memory>
#include <unordered_map>
#include <utility>

// One list of indexed triangles in a vertex and an index buffer: where it starts in each (bytes),
// how many indices it has and their size. The buffers belong to whoever holds the list.
struct QcGpuList
{
    QRhiBuffer *vertices = nullptr;
    QRhiBuffer *indices = nullptr;
    quint32 vertexOffset = 0;
    quint32 indexOffset = 0;
    quint32 indexCount = 0;
    QRhiCommandBuffer::IndexFormat indexFormat = QRhiCommandBuffer::IndexUInt32;
};

// A vertex and an index buffer on the GPU with what a draw needs from them.
struct QcGpuBuffers
{
    std::unique_ptr<QRhiBuffer> vertices;
    std::unique_ptr<QRhiBuffer> indices;
    quint32 indexCount = 0;
    QRhiCommandBuffer::IndexFormat indexFormat = QRhiCommandBuffer::IndexUInt32;
    quint64 bytes() const { return (vertices ? vertices->size() : 0) + (indices ? indices->size() : 0); }
    QcGpuList list() const { return {vertices.get(), indices.get(), 0, 0, indexCount, indexFormat}; }

    // Uploads an entity mesh into dynamic buffers that grow on demand and are reused every frame;
    // nothing to draw without a mesh. The bytes uploaded.
    [[nodiscard]] quint32 uploadDynamic(QRhi *rhi, QRhiResourceUpdateBatch *updates, const QcEntityMesh *mesh);
};

// The lists of one section on the GPU, all in one vertex and one index buffer (QcPackedMesh,
// decision E-813).
struct QcGpuMesh
{
    std::unique_ptr<QRhiBuffer> vertexBuffer;
    std::unique_ptr<QRhiBuffer> indexBuffer;
    QcGpuList opaque;
    // Cut-out faces of the opaque pass (alpha-tested, QcChunkMesh).
    QcGpuList cutout;
    QcGpuList translucent;
    QcGpuList water;
    // Faces towards the void below the world, drawn only from below y = 0 (QcChunkMesh).
    QcGpuList underside;
    // Water and other translucent faces in the mesher's order (QcChunkMesh::translucentRuns).
    QList<quint32> translucentRuns;
    QVector3D boundsMin;
    QVector3D boundsMax;
    // GPU bytes of the section's two buffers (section buffer statistics).
    quint64 bytes() const { return (vertexBuffer ? vertexBuffer->size() : 0) + (indexBuffer ? indexBuffer->size() : 0); }
    QVector3D centre() const { return (boundsMin + boundsMax) * 0.5f; }
    // Whether a frame draws anything of the section: any list with faces; the underside only from
    // below the world (a section of plants or leaves alone has only cut-out faces, decision E-756).
    bool isDrawable(bool belowWorld) const
    {
        return opaque.indexCount > 0 || cutout.indexCount > 0 || translucent.indexCount > 0 || water.indexCount > 0
               || (belowWorld && underside.indexCount > 0);
    }
};

// The sections of the world on the GPU, one mesh per section key: the meshes received since the
// last frame are queued and uploaded in one resource update batch, together with their per-cell
// light when the frame plan reads it (QcSectionLightPool). Drawing them is QcWorldRenderer's part.
// Render thread only.
class QcGpuMeshStore
{
public:
    using Meshes = std::unordered_map<quint64, QcGpuMesh>;

    // Forgets every section, for a new QRhi; `sectionLight` says whether the plan reads the light pool.
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
    // GPU bytes of all sections.
    quint64 bytes() const { return m_bytes; }
    // Section buffers written directly (QcBufferWrite) since the last reset().
    quint64 directWrites() const { return m_directWrites; }

private:
    // The section's lists in one vertex and one index buffer; empty without faces.
    QcGpuMesh createMesh(QRhiResourceUpdateBatch *updates, const QcChunkMesh &mesh);
    // A new section buffer of `kind` (vertex or index); null when the QRhi cannot create it.
    std::unique_ptr<QRhiBuffer> createBuffer(QRhiBuffer::UsageFlag kind, quint32 size) const;
    // Writes `data` into a new section buffer: directly where possible, otherwise through `updates`.
    void fill(QRhiResourceUpdateBatch *updates, QRhiBuffer *buffer, const QByteArray &data);
    // The light of a received section into the pool, when the plan's steps read it.
    void uploadSectionLight(QRhiResourceUpdateBatch *updates, quint64 key, const QcChunkMesh *mesh);
    void release(quint64 key);
    // The usage of a section buffer of `kind` (vertex or index) on this QRhi.
    QRhiBuffer::UsageFlags usage(QRhiBuffer::UsageFlag kind) const;

    QRhi *m_rhi = nullptr;
    bool m_sectionLight = false;
    // Section buffers are written directly (QcBufferWrite) with the storage usage (usage()).
    bool m_directWrite = false;
    // A failed direct write was reported (once per QRhi).
    bool m_warnedWriteFallback = false;
    quint64 m_directWrites = 0;
    Meshes m_meshes;
    // The latest mesh of each section changed since the last frame (null: removed). One entry per
    // section: a second upload of a section in the same resource update batch would release the
    // buffers of the first while the batch still refers to them (decision E-413).
    std::unordered_map<quint64, std::shared_ptr<const QcChunkMesh>> m_uploads;
    bool m_clearAll = false;
    // Per-cell light of the sections, filled only when m_sectionLight is set.
    QcSectionLightPool m_lightPool;
    quint64 m_bytes = 0;
};
