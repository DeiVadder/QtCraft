#include "app/rendering/QcGpuMeshStore.h"

#include "app/debug/QcAppLogging.h"
#include "app/rendering/QcBufferWrite.h"
#include "clientcore/meshing/QcPackedMesh.h"

#include <algorithm>
#include <limits>

void QcGpuMeshStore::reset(QRhi *rhi, bool sectionLight)
{
    m_meshes.clear();
    m_lightPool.reset();
    m_bytes = 0;
    m_rhi = rhi;
    m_sectionLight = sectionLight;
    m_directWrite = QcBufferWrite::isSupported(rhi);
    m_warnedWriteFallback = false;
    m_directWrites = 0;
}

QRhiBuffer::UsageFlags QcGpuMeshStore::usage(QRhiBuffer::UsageFlag kind) const
{
    // Without the storage usage QRhi's Metal backend keeps every buffer once per frame in flight;
    // section buffers are written once and never again, so one copy is safe, and QcBufferWrite
    // needs it (decision E-813).
    return m_directWrite ? QRhiBuffer::UsageFlags(kind | QRhiBuffer::StorageBuffer) : QRhiBuffer::UsageFlags(kind);
}

std::unique_ptr<QRhiBuffer> QcGpuMeshStore::createBuffer(QRhiBuffer::UsageFlag kind, quint32 size) const
{
    std::unique_ptr<QRhiBuffer> buffer(m_rhi->newBuffer(QRhiBuffer::Immutable, usage(kind), size));
    if (!buffer->create()) {
        qCWarning(lcRenderer).nospace() << "Section buffer of " << size << " bytes could not be created";
        return nullptr;
    }
    return buffer;
}

void QcGpuMeshStore::fill(QRhiResourceUpdateBatch *updates, QRhiBuffer *buffer, const QByteArray &data)
{
    const auto size = quint32(data.size());
    if (m_directWrite) {
        if (QcBufferWrite::write(buffer, data.constData(), size)) {
            ++m_directWrites;
            return;
        }
        // Expected to work for every section buffer on this QRhi; the batch upload below still fills it.
        if (!m_warnedWriteFallback) {
            m_warnedWriteFallback = true;
            qCWarning(lcRenderer) << "Direct section buffer write failed, uploading through resource update batches";
        }
    }
    updates->uploadStaticBuffer(buffer, data.constData());
}

QcGpuMesh QcGpuMeshStore::createMesh(QRhiResourceUpdateBatch *updates, const QcChunkMesh &mesh)
{
    QcGpuMesh gpuMesh;
    const QcPackedMesh packed = QcPackedMesh::pack(mesh);
    if (packed.isEmpty()) {
        return gpuMesh;
    }

    // Both buffers exist before either is filled: a buffer released with an upload still queued in
    // the batch would crash the backend (decision E-413).
    gpuMesh.vertexBuffer = createBuffer(QRhiBuffer::VertexBuffer, quint32(packed.vertices.size()));
    gpuMesh.indexBuffer = createBuffer(QRhiBuffer::IndexBuffer, quint32(packed.indices.size()));
    if (!gpuMesh.vertexBuffer || !gpuMesh.indexBuffer) {
        return QcGpuMesh(); // nothing to draw; createBuffer() reported it
    }
    fill(updates, gpuMesh.vertexBuffer.get(), packed.vertices);
    fill(updates, gpuMesh.indexBuffer.get(), packed.indices);

    const auto list = [&](QcPackedMesh::List which) {
        const QcPackedMesh::Place &place = packed.place(which);
        return QcGpuList{gpuMesh.vertexBuffer.get(),
                         gpuMesh.indexBuffer.get(),
                         place.vertexOffset,
                         place.indexOffset,
                         place.indexCount,
                         place.shortIndices ? QRhiCommandBuffer::IndexUInt16 : QRhiCommandBuffer::IndexUInt32};
    };
    gpuMesh.opaque = list(QcPackedMesh::List::Opaque);
    gpuMesh.cutout = list(QcPackedMesh::List::Cutout);
    gpuMesh.translucent = list(QcPackedMesh::List::Translucent);
    gpuMesh.water = list(QcPackedMesh::List::Water);
    gpuMesh.underside = list(QcPackedMesh::List::Underside);
    return gpuMesh;
}

void QcGpuMeshStore::release(quint64 key)
{
    if (const auto found = m_meshes.find(key); found != m_meshes.end()) {
        m_bytes -= found->second.bytes();
        m_meshes.erase(found);
    }
}

quint64 QcGpuMeshStore::uploadQueued(QRhiResourceUpdateBatch *updates)
{
    if (m_clearAll) {
        m_meshes.clear();
        m_lightPool.reset();
        m_bytes = 0;
        m_clearAll = false;
    }

    quint64 uploaded = 0;
    for (auto &[key, mesh] : m_uploads) {
        release(key);
        uploadSectionLight(updates, key, mesh.get());
        if (!mesh) {
            continue;
        }

        QcGpuMesh gpuMesh = createMesh(updates, *mesh);
        gpuMesh.translucentRuns = mesh->translucentRuns;

        QVector3D low(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                      std::numeric_limits<float>::max());
        QVector3D high = -low;
        for (const QList<QcBlockVertex> *list :
             {&mesh->opaqueVertices, &mesh->cutoutVertices, &mesh->translucentVertices, &mesh->waterVertices, &mesh->undersideVertices}) {
            for (const QcBlockVertex &vertex : *list) {
                low = QVector3D(std::min(low.x(), vertex.x), std::min(low.y(), vertex.y), std::min(low.z(), vertex.z));
                high = QVector3D(std::max(high.x(), vertex.x), std::max(high.y(), vertex.y), std::max(high.z(), vertex.z));
            }
        }

        gpuMesh.boundsMin = low;
        gpuMesh.boundsMax = high;
        const quint64 bytes = gpuMesh.bytes();
        m_bytes += bytes;
        uploaded += bytes;
        m_meshes.insert_or_assign(key, std::move(gpuMesh));
    }

    m_uploads.clear();
    return uploaded;
}

void QcGpuMeshStore::uploadSectionLight(QRhiResourceUpdateBatch *updates, quint64 key, const QcChunkMesh *mesh)
{
    if (!m_sectionLight || !QcSectionLightPool::isSupported(m_rhi)) {
        return;
    }
    if (!mesh) {
        m_lightPool.release(key);
        return;
    }

    const int pages = m_lightPool.pageCount();
    m_lightPool.update(m_rhi, updates, key, mesh->light);
    if (m_lightPool.pageCount() != pages) {
        qCInfo(lcRenderer).nospace() << "Section light: " << m_lightPool.pageCount() << " pages, " << m_lightPool.usedSlots()
                                     << " slots used, " << m_lightPool.sections() << " sections, " << m_lightPool.bytes() / 1024 << " KiB";
    }
}

quint32 QcGpuBuffers::uploadDynamic(QRhi *rhi, QRhiResourceUpdateBatch *updates, const QcEntityMesh *mesh)
{
    indexCount = 0;
    if (!mesh || mesh->indices.isEmpty()) {
        return 0;
    }

    const auto vertexBytes = quint32(mesh->vertices.size() * qsizetype(sizeof(QcEntityVertex)));
    const auto indexBytes = quint32(mesh->indices.size() * qsizetype(sizeof(quint32)));
    if (!vertices || vertices->size() < vertexBytes) {
        vertices.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, vertexBytes * 2));
        vertices->create();
    }
    if (!indices || indices->size() < indexBytes) {
        indices.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, indexBytes * 2));
        indices->create();
    }

    updates->updateDynamicBuffer(vertices.get(), 0, vertexBytes, mesh->vertices.constData());
    updates->updateDynamicBuffer(indices.get(), 0, indexBytes, mesh->indices.constData());
    indexCount = quint32(mesh->indices.size());
    return vertexBytes + indexBytes;
}
