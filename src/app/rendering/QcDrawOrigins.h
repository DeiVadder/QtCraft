#pragma once

#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"

#include <QList>
#include <QVector4D>
#include <rhi/qrhi.h>

#include <memory>

// The offsets of a frame's draws from the camera (decision E-883): one QcRenderOrigin entry per draw
// (a section, the entity meshes, the hand) in a dynamic vertex buffer that the block and entity
// shaders read per instance at `location` from vertex input binding 1. A draw picks its entry as its
// first instance where the GPU supports it (QRhi::BaseInstance), otherwise by binding the buffer at
// the entry (inputAt()). Render thread only.
class QcDrawOrigins
{
public:
    static constexpr quint32 stride = sizeof(QVector4D);
    // The shaders' input location of the entry (after the vertex attributes of both formats).
    static constexpr int location = 6;
    static constexpr int binding = 1;

    // Forgets the entries of the last frame; the new ones are offsets from `camera`.
    void begin(const QcVec3 &camera);
    // The entry of a draw of geometry relative to `origin` (QcEntityMesh::origin).
    [[nodiscard]] quint32 add(const QcVec3 &origin);
    // The entry of a section whose vertices are relative to `corner` (QcChunkMesh::origin), with
    // the corner for the shaders' hashes (QcRenderOrigin::sectionCell).
    [[nodiscard]] quint32 addSection(QcBlockPos corner);
    // Uploads the entries into a buffer that grows on demand; the bytes uploaded.
    [[nodiscard]] quint32 upload(QRhi *rhi, QRhiResourceUpdateBatch *updates);
    // Drops the buffer (a new QRhi).
    void reset();

    // The input of binding 1 for a draw of `entry`: the buffer from its start with base instances,
    // else from the entry.
    [[nodiscard]] QRhiCommandBuffer::VertexInput inputAt(quint32 entry, bool baseInstance) const;
    qsizetype size() const { return m_entries.size(); }
    const QList<QVector4D> &entries() const { return m_entries; }

    // Binding 1 (per instance) and its attribute, added to a pipeline's vertex input layout.
    [[nodiscard]] static QRhiVertexInputLayout withOrigins(const QRhiVertexInputLayout &layout);

private:
    QcVec3 m_camera;
    QList<QVector4D> m_entries;
    std::unique_ptr<QRhiBuffer> m_buffer;
};
