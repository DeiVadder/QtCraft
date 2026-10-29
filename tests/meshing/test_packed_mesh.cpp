// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/meshing/QcPackedMesh.h"

#include <QTest>

#include <cstring>

namespace {

// `count` vertices whose x is `first`, `first` + 1, ...
QList<QcBlockVertex> vertices(int count, float first)
{
    QList<QcBlockVertex> list;
    for (int i = 0; i < count; ++i) {
        QcBlockVertex vertex;
        vertex.x = first + static_cast<float>(i);
        list.append(vertex);
    }
    return list;
}

QcBlockVertex vertexAt(const QcPackedMesh &packed, quint32 offset, int index)
{
    QcBlockVertex vertex;
    std::memcpy(&vertex, packed.vertices.constData() + offset + index * static_cast<qsizetype>(sizeof(QcBlockVertex)), sizeof(vertex));
    return vertex;
}

quint16 shortIndexAt(const QcPackedMesh &packed, quint32 offset, int index)
{
    quint16 value = 0;
    std::memcpy(&value, packed.indices.constData() + offset + index * static_cast<qsizetype>(sizeof(quint16)), sizeof(value));
    return value;
}

quint32 longIndexAt(const QcPackedMesh &packed, quint32 offset, int index)
{
    quint32 value = 0;
    std::memcpy(&value, packed.indices.constData() + offset + index * static_cast<qsizetype>(sizeof(quint32)), sizeof(value));
    return value;
}

} // namespace

class TestPackedMesh : public QObject
{
    Q_OBJECT

private slots:
    void emptyMeshPacksToNothing()
    {
        const QcPackedMesh packed = QcPackedMesh::pack(QcChunkMesh());
        QVERIFY(packed.isEmpty());
        QVERIFY(packed.vertices.isEmpty());
        for (const QcPackedMesh::Place &place : packed.places) {
            QCOMPARE(place.indexCount, 0u);
        }
    }

    void listsFollowEachOtherInListOrder()
    {
        QcChunkMesh mesh;
        mesh.opaqueVertices = vertices(4, 0.0f);
        mesh.opaqueIndices = {0, 1, 2, 0, 2, 3};
        // Three indices: two bytes each, so the next list starts after two bytes of padding.
        mesh.waterVertices = vertices(3, 100.0f);
        mesh.waterIndices = {2, 1, 0};
        mesh.undersideVertices = vertices(4, 200.0f);
        mesh.undersideIndices = {3, 2, 1, 3, 1, 0};

        const QcPackedMesh packed = QcPackedMesh::pack(mesh);
        const QcPackedMesh::Place &opaque = packed.place(QcPackedMesh::List::Opaque);
        const QcPackedMesh::Place &water = packed.place(QcPackedMesh::List::Water);
        const QcPackedMesh::Place &underside = packed.place(QcPackedMesh::List::Underside);
        QCOMPARE(packed.place(QcPackedMesh::List::Cutout).indexCount, 0u);
        QCOMPARE(packed.place(QcPackedMesh::List::Translucent).indexCount, 0u);

        QCOMPARE(opaque.vertexOffset, 0u);
        QCOMPARE(water.vertexOffset, 4u * quint32(sizeof(QcBlockVertex)));
        QCOMPARE(underside.vertexOffset, 7u * quint32(sizeof(QcBlockVertex)));
        QCOMPARE(packed.vertices.size(), 11 * qsizetype(sizeof(QcBlockVertex)));

        QCOMPARE(opaque.indexOffset, 0u);
        QCOMPARE(water.indexOffset, 12u);
        QCOMPARE(underside.indexOffset, 20u);
        QCOMPARE(packed.indices.size(), 32);
        QCOMPARE(water.indexCount, 3u);
        QVERIFY(water.shortIndices);
    }

    void dataLandsAtItsPlace()
    {
        QcChunkMesh mesh;
        mesh.cutoutVertices = vertices(3, 10.0f);
        mesh.cutoutIndices = {0, 1, 2};
        mesh.translucentVertices = vertices(4, 20.0f);
        mesh.translucentIndices = {3, 2, 1};

        const QcPackedMesh packed = QcPackedMesh::pack(mesh);
        const QcPackedMesh::Place &cutout = packed.place(QcPackedMesh::List::Cutout);
        const QcPackedMesh::Place &translucent = packed.place(QcPackedMesh::List::Translucent);
        QCOMPARE(vertexAt(packed, cutout.vertexOffset, 2).x, 12.0f);
        QCOMPARE(vertexAt(packed, translucent.vertexOffset, 0).x, 20.0f);
        QCOMPARE(vertexAt(packed, translucent.vertexOffset, 3).x, 23.0f);
        QCOMPARE(shortIndexAt(packed, cutout.indexOffset, 2), quint16(2));
        QCOMPARE(shortIndexAt(packed, translucent.indexOffset, 0), quint16(3));
        // The padding after the cut-out list's three indices is zero.
        QCOMPARE(shortIndexAt(packed, cutout.indexOffset, 3), quint16(0));
    }

    void listOf65536VerticesTakesTwoByteIndices()
    {
        // The largest list whose indices 0 .. 65535 still fit into 16 bits.
        QcChunkMesh mesh;
        mesh.opaqueVertices = vertices(65536, 0.0f);
        mesh.opaqueIndices = {65535, 40000, 0, 1, 2, 2};
        mesh.cutoutVertices = vertices(3, 0.0f);
        mesh.cutoutIndices = {0, 1, 2};

        const QcPackedMesh packed = QcPackedMesh::pack(mesh);
        const QcPackedMesh::Place &opaque = packed.place(QcPackedMesh::List::Opaque);
        QVERIFY(opaque.shortIndices);
        QCOMPARE(shortIndexAt(packed, opaque.indexOffset, 0), quint16(65535));
        QCOMPARE(shortIndexAt(packed, opaque.indexOffset, 1), quint16(40000));
        QCOMPARE(shortIndexAt(packed, opaque.indexOffset, 5), quint16(2));
        // Six two-byte indices: the cut-out list follows without padding.
        QCOMPARE(packed.place(QcPackedMesh::List::Cutout).indexOffset, 12u);
        QCOMPARE(packed.indices.size(), 20);
    }

    void largeListsKeepFourByteIndices()
    {
        QcChunkMesh mesh;
        mesh.opaqueVertices = vertices(65537, 0.0f);
        mesh.opaqueIndices = {65536, 1, 0};
        mesh.cutoutVertices = vertices(3, 0.0f);
        mesh.cutoutIndices = {0, 1, 2};

        const QcPackedMesh packed = QcPackedMesh::pack(mesh);
        const QcPackedMesh::Place &opaque = packed.place(QcPackedMesh::List::Opaque);
        const QcPackedMesh::Place &cutout = packed.place(QcPackedMesh::List::Cutout);
        // 65537 vertices: index 65536 needs 32 bits.
        QVERIFY(!opaque.shortIndices);
        QVERIFY(cutout.shortIndices);
        QCOMPARE(longIndexAt(packed, opaque.indexOffset, 0), 65536u);
        QCOMPARE(longIndexAt(packed, opaque.indexOffset, 1), 1u);
        QCOMPARE(cutout.indexOffset, 12u);
        QCOMPARE(shortIndexAt(packed, cutout.indexOffset, 1), quint16(1));
    }
};

QTEST_GUILESS_MAIN(TestPackedMesh)
#include "test_packed_mesh.moc"
