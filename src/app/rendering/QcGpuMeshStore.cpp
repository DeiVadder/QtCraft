#include "app/rendering/QcGpuMeshStore.h"

#include "app/debug/QcAppLogging.h"
#include "clientcore/meshing/QcIndexPacking.h"

#include <algorithm>
#include <limits>

void QcGpuMeshStore::reset(QRhi *rhi, bool sectionLight)
{
    m_meshes.clear();
    m_lightPool.reset();
    m_bytes = 0;
    m_rhi = rhi;
    m_sectionLight = sectionLight;
}

QcGpuBuffers QcGpuMeshStore::createBuffers(QRhiResourceUpdateBatch *updates, const QList<QcBlockVertex> &vertices,
                                           const QList<quint32> &indices)
{
    QcGpuBuffers buffers;
    if (indices.isEmpty()) {
        return buffers;
    }

    const quint32 vertexBytes = quint32(vertices.size() * qsizetype(sizeof(QcBlockVertex)));
    buffers.vertices.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, vertexBytes));
    buffers.vertices->create();
    updates->uploadStaticBuffer(buffers.vertices.get(), vertices.constData());

    // Almost every section has at most 65536 vertices; its indices then take two bytes each.
    if (QcIndexPacking::fitsShortIndices(vertices.size())) {
        const QByteArray shortIndices = QcIndexPacking::toShortIndices(indices);
        buffers.indices.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, quint32(shortIndices.size())));
        buffers.indices->create();
        updates->uploadStaticBuffer(buffers.indices.get(), shortIndices.constData());
        buffers.indexFormat = QRhiCommandBuffer::IndexUInt16;
    } else {
        const quint32 indexBytes = quint32(indices.size() * qsizetype(sizeof(quint32)));
        buffers.indices.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, indexBytes));
        buffers.indices->create();
        updates->uploadStaticBuffer(buffers.indices.get(), indices.constData());
    }

    buffers.indexCount = quint32(indices.size());
    return buffers;
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

        QcGpuMesh gpuMesh;
        gpuMesh.opaque = createBuffers(updates, mesh->opaqueVertices, mesh->opaqueIndices);
        gpuMesh.translucent = createBuffers(updates, mesh->translucentVertices, mesh->translucentIndices);
        gpuMesh.water = createBuffers(updates, mesh->waterVertices, mesh->waterIndices);
        gpuMesh.underside = createBuffers(updates, mesh->undersideVertices, mesh->undersideIndices);
        gpuMesh.translucentRuns = mesh->translucentRuns;

        QVector3D low(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                      std::numeric_limits<float>::max());
        QVector3D high = -low;
        for (const QList<QcBlockVertex> *list : {&mesh->opaqueVertices, &mesh->translucentVertices, &mesh->waterVertices, &mesh->undersideVertices}) {
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
