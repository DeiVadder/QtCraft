#include "app/rendering/QcGpuMeshStore.h"

#include "app/debug/QcAppLogging.h"
#include "app/rendering/QcBufferWrite.h"
#include "clientcore/meshing/QcPackedMesh.h"

#include <algorithm>
#include <limits>
#include <optional>

void QcGpuMeshStore::reset(QRhi *rhi, bool sectionLight)
{
    m_meshes.clear();
    m_lightPool.reset();
    m_rhi = rhi;
    m_sectionLight = sectionLight;
    m_directWrite = QcBufferWrite::isSupported(rhi);
    m_warnedWriteFallback = false;
    m_directWrites = 0;
    // Pages shared by many sections only where they are written directly (Metal with the storage
    // usage, so the backend keeps one copy instead of one per frame in flight): the direct write
    // touches just the section's range, which no draw in flight reads. Through a resource update
    // batch the other backends would stage or copy a whole page for every section (Vulkan allocates
    // a staging buffer of the buffer's size per frame), so there every section keeps a page of its
    // own, as before the arena (decisions E-813, E-820).
    if (m_directWrite) {
        m_arena.reset(rhi, QRhiBuffer::StorageBuffer, QcSectionArena::Layout::Shared);
    } else {
        m_arena.reset(rhi, QRhiBuffer::UsageFlags(), QcSectionArena::Layout::PerSection);
    }
}

void QcGpuMeshStore::fill(QRhiResourceUpdateBatch *updates, QRhiBuffer *buffer, quint32 offset, const QByteArray &data)
{
    const auto size = static_cast<quint32>(data.size());
    if (m_directWrite) {
        if (QcBufferWrite::write(buffer, offset, data.constData(), size)) {
            ++m_directWrites;
            return;
        }
        // Expected to work for every section page on this QRhi; the batch upload below still fills it.
        if (!m_warnedWriteFallback) {
            m_warnedWriteFallback = true;
            qCWarning(lcRenderer) << "Direct section buffer write failed, uploading through resource update batches";
        }
    }
    updates->uploadStaticBuffer(buffer, offset, size, data.constData());
}

namespace {

// A list of the packed mesh at its place in the slot.
QcGpuList gpuList(const QcArenaSlot &slot, QcPackedMesh::Place place)
{
    const quint32 vertexOffset = slot.vertexOffset + place.vertexOffset;
    const quint32 indexOffset = slot.indexOffset + place.indexOffset;
    const quint32 indexSize = place.shortIndices ? 2 : 4;
    return QcGpuList{slot.vertices,
                     slot.indices,
                     vertexOffset,
                     indexOffset,
                     place.indexCount,
                     place.shortIndices ? QRhiCommandBuffer::IndexUInt16 : QRhiCommandBuffer::IndexUInt32,
                     indexOffset / indexSize,
                     static_cast<qint32>(vertexOffset / QcSectionArena::vertexStride)};
}

} // namespace

QcGpuMesh QcGpuMeshStore::createMesh(QRhiResourceUpdateBatch *updates, const QcChunkMesh &mesh)
{
    QcGpuMesh gpuMesh;
    const QcPackedMesh packed = QcPackedMesh::pack(mesh);
    if (packed.isEmpty()) {
        return gpuMesh;
    }

    const std::optional<QcArenaSlot> slot = m_arena.allocate(static_cast<quint32>(packed.vertices.size()), static_cast<quint32>(packed.indices.size()));
    if (!slot) {
        return gpuMesh; // nothing to draw; the arena reported it
    }
    fill(updates, slot->vertices, slot->vertexOffset, packed.vertices);
    fill(updates, slot->indices, slot->indexOffset, packed.indices);

    gpuMesh.slot = *slot;
    gpuMesh.opaque = gpuList(*slot, packed.place(QcPackedMesh::List::Opaque));
    gpuMesh.cutout = gpuList(*slot, packed.place(QcPackedMesh::List::Cutout));
    gpuMesh.translucent = gpuList(*slot, packed.place(QcPackedMesh::List::Translucent));
    gpuMesh.water = gpuList(*slot, packed.place(QcPackedMesh::List::Water));
    gpuMesh.underside = gpuList(*slot, packed.place(QcPackedMesh::List::Underside));
    return gpuMesh;
}

void QcGpuMeshStore::release(quint64 key)
{
    if (const auto found = m_meshes.find(key); found != m_meshes.end()) {
        m_arena.release(found->second.slot);
        m_meshes.erase(found);
    }
}

quint64 QcGpuMeshStore::uploadQueued(QRhiResourceUpdateBatch *updates)
{
    if (m_clearAll) {
        m_meshes.clear();
        m_arena.clear();
        m_lightPool.reset();
        m_clearAll = false;
    }
    m_arena.nextFrame();

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
        uploaded += gpuMesh.bytes();
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

    const auto vertexBytes = static_cast<quint32>(mesh->vertices.size() * static_cast<qsizetype>(sizeof(QcEntityVertex)));
    const auto indexBytes = static_cast<quint32>(mesh->indices.size() * static_cast<qsizetype>(sizeof(quint32)));
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
    indexCount = static_cast<quint32>(mesh->indices.size());
    return vertexBytes + indexBytes;
}
