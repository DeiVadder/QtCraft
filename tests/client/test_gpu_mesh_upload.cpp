// Section buffers written directly into the MTLBuffer (decision E-813): a section mesh uploaded
// through QcGpuMeshStore on an offscreen Metal QRhi holds on the GPU exactly what QcPackedMesh packs,
// at its slot in a page of the section arena shared with other sections (decision E-820). Skipped
// without a Metal device.

#include "app/rendering/QcBufferWrite.h"
#include "app/rendering/QcGpuMeshStore.h"
#include "clientcore/camera/QcRenderOrigin.h"
#include "clientcore/meshing/QcPackedMesh.h"

#include <QTest>
#include <rhi/qrhi.h>

#include <cstring>
#include <memory>

namespace {

// A section mesh with faces in three lists; the water list has an odd index count, so the next list
// starts after padding.
std::shared_ptr<const QcChunkMesh> sectionMesh()
{
    auto mesh = std::make_shared<QcChunkMesh>();
    for (int i = 0; i < 8; ++i) {
        QcBlockVertex vertex;
        vertex.x = static_cast<float>(i);
        vertex.y = static_cast<float>(i * 2);
        vertex.z = static_cast<float>(i * 3);
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

// A section mesh whose opaque list needs 4-byte indices (65537 vertices) and whose water and
// translucent faces alternate in runs of three indices (water, translucent, water).
std::shared_ptr<const QcChunkMesh> wideMesh()
{
    auto mesh = std::make_shared<QcChunkMesh>();
    for (int i = 0; i <= 65536; ++i) {
        QcBlockVertex vertex;
        vertex.x = static_cast<float>(i);
        mesh->opaqueVertices.append(vertex);
    }
    mesh->opaqueIndices = {65536, 1, 0, 65535, 2, 65534};
    for (int i = 0; i < 6; ++i) {
        QcBlockVertex vertex;
        vertex.y = static_cast<float>(10 + i);
        mesh->waterVertices.append(vertex);
        if (i < 3) {
            vertex.z = static_cast<float>(20 + i);
            mesh->translucentVertices.append(vertex);
        }
    }
    mesh->waterIndices = {0, 1, 2, 5, 4, 3};
    mesh->translucentIndices = {2, 0, 1};
    mesh->translucentRuns = {3, 3, 3};
    return mesh;
}

// The vertices a draw of `count` indices from `firstIndex` of `list` fetches, as the GPU does it from
// the start of the page's buffers: index `list.firstIndex + firstIndex + i` in the list's format,
// plus the base vertex, times the vertex stride.
QList<QcBlockVertex> fetched(const QByteArray &vertices, const QByteArray &indices, const QcGpuList &list, quint32 firstIndex, quint32 count)
{
    QList<QcBlockVertex> result;
    const bool shortIndices = list.indexFormat == QRhiCommandBuffer::IndexUInt16;
    for (quint32 i = 0; i < count; ++i) {
        const qsizetype at = static_cast<qsizetype>(list.firstIndex) + firstIndex + i;
        quint32 index = 0;
        if (shortIndices) {
            quint16 value = 0;
            std::memcpy(&value, indices.constData() + at * 2, 2);
            index = value;
        } else {
            std::memcpy(&index, indices.constData() + at * 4, 4);
        }
        QcBlockVertex vertex;
        const auto vertexAt = static_cast<qsizetype>(static_cast<quint32>(list.baseVertex) + index) * static_cast<qsizetype>(sizeof(QcBlockVertex));
        std::memcpy(&vertex, vertices.constData() + vertexAt, sizeof(QcBlockVertex));
        result.append(vertex);
    }
    return result;
}

// The vertices the mesher meant: `count` indices from `firstIndex` of a list, resolved on the CPU.
QList<QcBlockVertex> meant(const QList<QcBlockVertex> &vertices, const QList<quint32> &indices, quint32 firstIndex, quint32 count)
{
    QList<QcBlockVertex> result;
    const auto last = static_cast<qsizetype>(count);
    for (qsizetype i = 0; i < last; ++i) {
        result.append(vertices[static_cast<qsizetype>(indices[static_cast<qsizetype>(firstIndex) + i])]);
    }
    return result;
}

bool sameVertex(const QcBlockVertex &a, const QcBlockVertex &b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z && a.u == b.u && a.v == b.v && a.light == b.light;
}

bool sameVertices(const QList<QcBlockVertex> &a, const QList<QcBlockVertex> &b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (qsizetype i = 0; i < a.size(); ++i) {
        if (!sameVertex(a[i], b[i])) {
            return false;
        }
    }
    return true;
}

// One frame of the store: the queued meshes go up; the bytes uploaded.
quint64 uploadFrame(QRhi *rhi, QcGpuMeshStore &store)
{
    QRhiCommandBuffer *commands = nullptr;
    if (rhi->beginOffscreenFrame(&commands) != QRhi::FrameOpSuccess) {
        return 0;
    }
    QRhiResourceUpdateBatch *updates = rhi->nextResourceUpdateBatch();
    const quint64 uploaded = store.uploadQueued(updates);
    commands->resourceUpdate(updates);
    rhi->endOffscreenFrame();
    return uploaded;
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
        // Direct writes allow pages shared by many sections (decision E-820).
        QCOMPARE(store.layout(), QcSectionArena::Layout::Shared);
        const std::shared_ptr<const QcChunkMesh> mesh = sectionMesh();
        constexpr quint64 key = 42;
        store.queue(key, mesh);

        const quint64 uploaded = uploadFrame(m_rhi.get(), store);

        // Both parts went straight into GPU memory, not through the batch.
        QCOMPARE(store.directWrites(), quint64(2));
        QCOMPARE(store.meshes().size(), std::size_t(1));
        const QcGpuMesh &gpuMesh = store.meshes().at(key);
        QVERIFY(gpuMesh.slot.isValid());
        QCOMPARE(uploaded, gpuMesh.bytes());

        const QcPackedMesh packed = QcPackedMesh::pack(*mesh);
        const QByteArray vertices = readBack(m_rhi.get(), gpuMesh.slot.vertices);
        const QByteArray indices = readBack(m_rhi.get(), gpuMesh.slot.indices);
        QCOMPARE(gpuMesh.slot.vertexBytes, quint32(packed.vertices.size()));
        QCOMPARE(gpuMesh.slot.indexBytes, quint32(packed.indices.size()));
        QVERIFY(vertices.mid(gpuMesh.slot.vertexOffset, packed.vertices.size()) == packed.vertices);
        QVERIFY(indices.mid(gpuMesh.slot.indexOffset, packed.indices.size()) == packed.indices);

        // The lists point at their places in the packed data, as byte offsets and as the first
        // index and base vertex of a draw from the start of the page.
        const QcPackedMesh::Place &water = packed.place(QcPackedMesh::List::Water);
        QCOMPARE(gpuMesh.water.vertexOffset, gpuMesh.slot.vertexOffset + water.vertexOffset);
        QCOMPARE(gpuMesh.water.indexOffset, gpuMesh.slot.indexOffset + water.indexOffset);
        QCOMPARE(gpuMesh.water.indexCount, 3u);
        QCOMPARE(gpuMesh.water.indexFormat, QRhiCommandBuffer::IndexUInt16);
        QCOMPARE(gpuMesh.water.firstIndex, gpuMesh.water.indexOffset / 2);
        QCOMPARE(gpuMesh.water.baseVertex, qint32(gpuMesh.water.vertexOffset / 24));
    }

    // Many sections share one page: each holds its own data at its own slot.
    void sectionsShareAPage()
    {
        if (!m_rhi) {
            QSKIP("no Metal device for the offscreen QRhi");
        }
        QcGpuMeshStore store;
        store.reset(m_rhi.get(), false);
        const std::shared_ptr<const QcChunkMesh> mesh = sectionMesh();
        for (quint64 key = 1; key <= 3; ++key) {
            store.queue(key, mesh);
        }
        uploadFrame(m_rhi.get(), store);

        QCOMPARE(store.pageCount(), 1);
        const QcGpuMesh &first = store.meshes().at(1);
        const QcGpuMesh &third = store.meshes().at(3);
        QCOMPARE(first.slot.vertices, third.slot.vertices);
        QVERIFY(first.slot.vertexOffset != third.slot.vertexOffset);
        QVERIFY(first.slot.indexOffset != third.slot.indexOffset);

        const QcPackedMesh packed = QcPackedMesh::pack(*mesh);
        const QByteArray vertices = readBack(m_rhi.get(), third.slot.vertices);
        const QByteArray indices = readBack(m_rhi.get(), third.slot.indices);
        QVERIFY(vertices.mid(third.slot.vertexOffset, packed.vertices.size()) == packed.vertices);
        QVERIFY(indices.mid(third.slot.indexOffset, packed.indices.size()) == packed.indices);
    }

    // The first index and base vertex of every list fetch the vertices the mesher meant, from the
    // start of a page that other sections share: with 2- and 4-byte indices, and for the runs of the
    // translucent pass, which draw a part of a list from an index within it.
    void drawParametersFetchTheMeantVertices()
    {
        if (!m_rhi) {
            QSKIP("no Metal device for the offscreen QRhi");
        }
        QcGpuMeshStore store;
        store.reset(m_rhi.get(), false);
        store.queue(1, sectionMesh());
        uploadFrame(m_rhi.get(), store);
        // In a later frame, so it lands behind the first section in the page.
        const std::shared_ptr<const QcChunkMesh> mesh = wideMesh();
        store.queue(2, mesh);
        uploadFrame(m_rhi.get(), store);

        const QcGpuMesh &gpuMesh = store.meshes().at(2);
        QCOMPARE(store.pageCount(), 1);
        QVERIFY(gpuMesh.slot.vertexOffset > 0);
        QCOMPARE(gpuMesh.opaque.indexFormat, QRhiCommandBuffer::IndexUInt32);
        QCOMPARE(gpuMesh.water.indexFormat, QRhiCommandBuffer::IndexUInt16);
        const QByteArray vertices = readBack(m_rhi.get(), gpuMesh.slot.vertices);
        const QByteArray indices = readBack(m_rhi.get(), gpuMesh.slot.indices);

        const QList<QcBlockVertex> opaque = fetched(vertices, indices, gpuMesh.opaque, 0, gpuMesh.opaque.indexCount);
        QVERIFY(sameVertices(opaque, meant(mesh->opaqueVertices, mesh->opaqueIndices, 0, 6)));
        // The runs: water 0..2, translucent 0..2, water 3..5 (QcWorldRenderer::drawTranslucentRuns).
        QVERIFY(sameVertices(fetched(vertices, indices, gpuMesh.water, 0, 3), meant(mesh->waterVertices, mesh->waterIndices, 0, 3)));
        QVERIFY(sameVertices(fetched(vertices, indices, gpuMesh.translucent, 0, 3),
                             meant(mesh->translucentVertices, mesh->translucentIndices, 0, 3)));
        QVERIFY(sameVertices(fetched(vertices, indices, gpuMesh.water, 3, 3), meant(mesh->waterVertices, mesh->waterIndices, 3, 3)));
    }

    // A removed section's slot is reused only after the frames in flight that may still draw it;
    // a page without sections goes away (but for one kept in reserve).
    void removedSlotsWaitForTheFramesInFlight()
    {
        if (!m_rhi) {
            QSKIP("no Metal device for the offscreen QRhi");
        }
        QcGpuMeshStore store;
        store.reset(m_rhi.get(), false);
        const std::shared_ptr<const QcChunkMesh> mesh = sectionMesh();
        store.queue(1, mesh);
        uploadFrame(m_rhi.get(), store);
        const quint32 removedOffset = store.meshes().at(1).slot.vertexOffset;

        store.queue(1, nullptr);
        store.queue(2, mesh);
        uploadFrame(m_rhi.get(), store);
        QVERIFY(store.meshes().at(2).slot.vertexOffset != removedOffset);
        QCOMPARE(store.pageCount(), 1);

        store.queue(2, nullptr);
        const int framesInFlight = m_rhi->resourceLimit(QRhi::FramesInFlight);
        for (int frame = 0; frame <= framesInFlight + 1; ++frame) {
            uploadFrame(m_rhi.get(), store);
        }
        // The empty page stays in reserve; the removed slot is free again.
        QCOMPARE(store.pageCount(), 1);
        store.queue(3, mesh);
        uploadFrame(m_rhi.get(), store);
        QCOMPARE(store.pageCount(), 1);
        QCOMPARE(store.meshes().at(3).slot.vertexOffset, removedOffset);
        store.clearAll();
        uploadFrame(m_rhi.get(), store);
        QCOMPARE(store.pageCount(), 0);
        QCOMPARE(store.bytes(), quint64(0));
    }

    // Each section with faces holds an entry of the draw origins (decision E-883) from its upload to
    // its removal, a section without faces none; a removed section's entry goes to the next one, and
    // clearing the store keeps the entries the renderer added itself.
    void sectionsHoldTheirDrawOrigin()
    {
        if (!m_rhi) {
            QSKIP("no Metal device for the offscreen QRhi");
        }
        QcGpuMeshStore store;
        store.reset(m_rhi.get(), false);
        const quint32 hand = store.origins().add(QcVec3{});
        auto placed = std::make_shared<QcChunkMesh>(*sectionMesh());
        placed->origin = {1'000'000'000 + 48, 64, -1'000'000'000 + 16};
        store.queue(1, placed);
        store.queue(2, std::make_shared<const QcChunkMesh>());
        uploadFrame(m_rhi.get(), store);

        const quint32 entry = store.meshes().at(1).drawOrigin;
        QVERIFY(entry != hand);
        QCOMPARE(store.meshes().at(2).drawOrigin, QcDrawOrigins::none);
        QCOMPARE(store.origins().size(), 2);
        const QcVec3 camera{1e9 + 40.5, 70.0, -1e9 + 0.5};
        const QVector3D drawCamera = store.origins().follow(camera);
        QCOMPARE(store.origins().at(entry).toVector3D() - drawCamera, QVector3D(7.5f, -6.0f, 15.5f));
        QCOMPARE(store.origins().at(entry).w(), QcRenderOrigin::sectionCell(placed->origin));

        store.queue(1, nullptr);
        uploadFrame(m_rhi.get(), store);
        QCOMPARE(store.origins().size(), 1);
        store.queue(3, placed);
        uploadFrame(m_rhi.get(), store);
        QCOMPARE(store.meshes().at(3).drawOrigin, entry);

        store.clearAll();
        uploadFrame(m_rhi.get(), store);
        QCOMPARE(store.origins().size(), 1);
        QCOMPARE(store.origins().at(hand), QcRenderOrigin::entry(QcVec3{}, store.origins().anchor()));
    }
};

QTEST_MAIN(TestGpuMeshUpload)
#include "test_gpu_mesh_upload.moc"
