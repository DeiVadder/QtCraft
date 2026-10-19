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

// A vertex and an index buffer on the GPU with what a draw needs from them.
struct QcGpuBuffers
{
    std::unique_ptr<QRhiBuffer> vertices;
    std::unique_ptr<QRhiBuffer> indices;
    quint32 indexCount = 0;
    QRhiCommandBuffer::IndexFormat indexFormat = QRhiCommandBuffer::IndexUInt32;
    quint64 bytes() const { return (vertices ? vertices->size() : 0) + (indices ? indices->size() : 0); }

    // Uploads an entity mesh into dynamic buffers that grow on demand and are reused every frame;
    // nothing to draw without a mesh. The bytes uploaded.
    [[nodiscard]] quint32 uploadDynamic(QRhi *rhi, QRhiResourceUpdateBatch *updates, const QcEntityMesh *mesh);
};

// The lists of one section on the GPU.
struct QcGpuMesh
{
    QcGpuBuffers opaque;
    QcGpuBuffers translucent;
    QcGpuBuffers water;
    // Faces towards the void below the world, drawn only from below y = 0 (QcChunkMesh).
    QcGpuBuffers underside;
    // Water and other translucent faces in the mesher's order (QcChunkMesh::translucentRuns).
    QList<quint32> translucentRuns;
    QVector3D boundsMin;
    QVector3D boundsMax;
    // GPU bytes of all four lists (section buffer statistics).
    quint64 bytes() const { return opaque.bytes() + translucent.bytes() + water.bytes() + underside.bytes(); }
    QVector3D centre() const { return (boundsMin + boundsMax) * 0.5f; }
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

private:
    QcGpuBuffers createBuffers(QRhiResourceUpdateBatch *updates, const QList<QcBlockVertex> &vertices, const QList<quint32> &indices);
    // The light of a received section into the pool, when the plan's steps read it.
    void uploadSectionLight(QRhiResourceUpdateBatch *updates, quint64 key, const QcChunkMesh *mesh);
    void release(quint64 key);

    QRhi *m_rhi = nullptr;
    bool m_sectionLight = false;
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
