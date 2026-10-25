#pragma once

#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"

#include <QList>
#include <QVector3D>
#include <QVector4D>
#include <rhi/qrhi.h>

#include <limits>
#include <memory>
#include <vector>

// The origins of the draws (decision E-883): one QcRenderOrigin entry per piece of geometry with an
// origin of its own (a section, the entity meshes, the hand) in a vertex buffer that the block and
// entity shaders read per instance at `location` from vertex input binding 1. An entry holds the
// origin's offset from the render anchor, a whole block near the camera that stays put until the
// camera has moved anchorReach away (QcRenderOrigin::anchorReach); the shaders subtract the camera's
// offset from the anchor, one uniform per frame (FrameUniforms::drawCamera). So an entry is written
// when its geometry comes, moves or the anchor moves, not every frame, and an upload carries only
// the changed entries. A draw picks its entry as its first instance where the GPU supports it
// (QRhi::BaseInstance), otherwise by binding the buffer at the entry (inputAt()). Render thread only.
class QcDrawOrigins
{
public:
    static constexpr quint32 stride = sizeof(QVector4D);
    // The shaders' input location of the entry (after the vertex attributes of both formats).
    static constexpr int location = 6;
    static constexpr int binding = 1;
    // No entry (a section without faces has none).
    static constexpr quint32 none = std::numeric_limits<quint32>::max();

    // Keeps the anchor within reach of `camera`: placed at the camera's block the first time and
    // whenever the camera has left its reach, which rewrites every entry. Returns the camera's
    // offset from the anchor, for the shaders.
    [[nodiscard]] QVector3D follow(const QcVec3 &camera);
    const QcVec3 &anchor() const { return m_anchor; }

    // A new entry for geometry relative to `origin` (QcEntityMesh::origin), kept until release().
    [[nodiscard]] quint32 add(const QcVec3 &origin);
    // A new entry for a section whose vertices are relative to `corner` (QcChunkMesh::origin), with
    // the corner for the shaders' hashes (QcRenderOrigin::sectionCell).
    [[nodiscard]] quint32 addSection(QcBlockPos corner);
    // The entry's geometry is now relative to `origin`; uploaded again only when that changes it.
    void move(quint32 entry, const QcVec3 &origin);
    // The entry is free for the next add().
    void release(quint32 entry);
    // Uploads the entries written since the last upload (all of them into a new buffer, which grows
    // on demand); the bytes uploaded.
    [[nodiscard]] quint32 upload(QRhi *rhi, QRhiResourceUpdateBatch *updates);
    // Drops the entries and the buffer (a new QRhi).
    void reset();

    // The input of binding 1 for a draw of `entry`: the buffer from its start with base instances,
    // else from the entry.
    [[nodiscard]] QRhiCommandBuffer::VertexInput inputAt(quint32 entry, bool baseInstance) const;
    // The entry as the shaders read it: xyz the origin's offset from the anchor, w the section cell.
    QVector4D at(quint32 entry) const { return m_entries.at(entry); }
    // Entries in use.
    qsizetype size() const { return m_entries.size() - static_cast<qsizetype>(m_free.size()); }

    // Binding 1 (per instance) and its attribute, added to a pipeline's vertex input layout.
    [[nodiscard]] static QRhiVertexInputLayout withOrigins(const QRhiVertexInputLayout &layout);

private:
    // An entry's origin in the world and its section cell (0 for other geometry).
    struct Origin
    {
        QcVec3 position;
        float cell = 0.0f;
    };

    [[nodiscard]] quint32 place(const Origin &origin);
    // Computes the entry from its origin and the anchor and queues it for the upload.
    void write(quint32 entry);
    // Uploads the queued entries as runs of neighbours; the bytes uploaded.
    [[nodiscard]] quint32 uploadQueued(QRhiResourceUpdateBatch *updates);

    QcVec3 m_anchor;
    bool m_anchored = false;
    QList<Origin> m_origins;
    QList<QVector4D> m_entries;
    std::vector<quint32> m_free;
    // Entries written since the last upload; all of them after a new anchor or buffer.
    std::vector<quint32> m_written;
    bool m_allWritten = false;
    std::unique_ptr<QRhiBuffer> m_buffer;
};
