// Section buffers written directly into the MTLBuffer (decision E-813): a section mesh uploaded
// through QcGpuMeshStore on an offscreen Metal QRhi holds on the GPU exactly what QcPackedMesh packs.
// Skipped without a Metal device.

#include "app/rendering/QcBufferWrite.h"
#include "app/rendering/QcGpuMeshStore.h"
#include "clientcore/meshing/QcPackedMesh.h"

#include <QTest>
#include <rhi/qrhi.h>

#include <memory>

namespace {

// A section mesh with faces in three lists; the water list has an odd index count, so the next list
// starts after padding.
std::shared_ptr<const QcChunkMesh> sectionMesh()
{
    auto mesh = std::make_shared<QcChunkMesh>();
    for (int i = 0; i < 8; ++i) {
        QcBlockVertex vertex;
        vertex.x = float(i);
        vertex.y = float(i * 2);
        vertex.z = float(i * 3);
        mesh->opaqueVertices.append(vertex);
        if (i < 3) {
            mesh->waterVertices.append(vertex);
        }
        mesh->undersideVertices.append(vertex);
    }
    mesh->opaqueIndices = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
    mesh->waterIndices = {2, 1, 0};
    mesh->undersideIndices = {7, 6, 5, 7, 5, 4};
    return mesh;
}

// The contents of `buffer` as the GPU holds them.
QByteArray readBack(QRhi *rhi, QRhiBuffer *buffer)
{
    QRhiCommandBuffer *commands = nullptr;
    if (rhi->beginOffscreenFrame(&commands) != QRhi::FrameOpSuccess) {
        return {};
    }
    QRhiReadbackResult result;
    QRhiResourceUpdateBatch *updates = rhi->nextResourceUpdateBatch();
    updates->readBackBuffer(buffer, 0, buffer->size(), &result);
    commands->resourceUpdate(updates);
    rhi->endOffscreenFrame();
    return result.data;
}

} // namespace

class TestGpuMeshUpload : public QObject
{
    Q_OBJECT

    std::unique_ptr<QRhi> m_rhi;

private slots:
    void initTestCase()
    {
#if QT_CONFIG(metal)
        QRhiMetalInitParams params;
        m_rhi.reset(QRhi::create(QRhi::Metal, &params));
#endif
    }

    void directWriteHoldsThePackedMesh()
    {
        if (!m_rhi) {
            QSKIP("no Metal device for the offscreen QRhi");
        }
        QVERIFY(QcBufferWrite::isSupported(m_rhi.get()));

        QcGpuMeshStore store;
        store.reset(m_rhi.get(), false);
        const std::shared_ptr<const QcChunkMesh> mesh = sectionMesh();
        constexpr quint64 key = 42;
        store.queue(key, mesh);

        QRhiCommandBuffer *commands = nullptr;
        QCOMPARE(m_rhi->beginOffscreenFrame(&commands), QRhi::FrameOpSuccess);
        QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();
        const quint64 uploaded = store.uploadQueued(updates);
        commands->resourceUpdate(updates);
        m_rhi->endOffscreenFrame();

        // Both buffers went straight into GPU memory, not through the batch.
        QCOMPARE(store.directWrites(), quint64(2));
        QCOMPARE(store.meshes().size(), std::size_t(1));
        const QcGpuMesh &gpuMesh = store.meshes().at(key);
        QVERIFY(gpuMesh.vertexBuffer);
        QVERIFY(gpuMesh.indexBuffer);
        QCOMPARE(uploaded, gpuMesh.bytes());

        const QcPackedMesh packed = QcPackedMesh::pack(*mesh);
        const QByteArray vertices = readBack(m_rhi.get(), gpuMesh.vertexBuffer.get());
        const QByteArray indices = readBack(m_rhi.get(), gpuMesh.indexBuffer.get());
        QCOMPARE(vertices.size(), packed.vertices.size());
        QCOMPARE(indices.size(), packed.indices.size());
        QVERIFY(vertices == packed.vertices);
        QVERIFY(indices == packed.indices);

        // The lists point at their places in the packed buffers.
        const QcPackedMesh::Place &water = packed.place(QcPackedMesh::List::Water);
        QCOMPARE(gpuMesh.water.vertexOffset, water.vertexOffset);
        QCOMPARE(gpuMesh.water.indexOffset, water.indexOffset);
        QCOMPARE(gpuMesh.water.indexCount, 3u);
        QCOMPARE(gpuMesh.water.indexFormat, QRhiCommandBuffer::IndexUInt16);
    }
};

QTEST_MAIN(TestGpuMeshUpload)
#include "test_gpu_mesh_upload.moc"
