#include "clientcore/maps/QcClientMaps.h"
#include "clientcore/maps/QcFramedMapAtlas.h"
#include "clientcore/maps/QcMapColors.h"
#include "clientcore/meshing/QcHangingMesher.h"
#include "common/maps/QcMapPixel.h"
#include "QcTestUtil.h"

#include <QTest>

#include <set>

using namespace Qt::StringLiterals;

namespace {

QcMsg::MapUpdate wholeMap(quint32 id, quint16 value)
{
    QcMsg::MapUpdate made;
    made.id = id;
    made.area = QcMapData::bounds();
    made.pixels = QList<quint16>(QcMapData::pixelCount, value);
    return made;
}

// A frame on the south face of the block north of (4, 64, 7).
QcHanging southFrame(int rotation)
{
    QcHanging hanging;
    hanging.face = QcBlockFace::South;
    hanging.cell = {4, 64, 7};
    hanging.rotation = quint8(rotation);
    return hanging;
}

// The texture coordinates at the corner of `mesh` nearest to `x`, `y` (the wall runs along x).
QPointF uvAt(const QcEntityMesh &mesh, double x, double y)
{
    const QcEntityVertex *nearest = nullptr;
    double best = 1e9;
    for (const QcEntityVertex &vertex : mesh.vertices) {
        const double distance = std::abs(vertex.x - x) + std::abs(vertex.y - y);
        if (distance < best) {
            best = distance;
            nearest = &vertex;
        }
    }
    return nearest ? QPointF(nearest->u, nearest->v) : QPointF(-1.0, -1.0);
}

} // namespace

// Maps in item frames on the client (decisions E-806, E-809): the quad over the whole face and its
// turns, which frames show a map, and the shared texture of the framed maps.
class TestFramedMaps : public QObject
{
    Q_OBJECT

    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    QcItemId item(const char *name) const { return m_content->itemId(QString::fromLatin1(name)).value_or(qcNoItem); }

private slots:
    void eightStepsShowFourTurns()
    {
        QCOMPARE(QcHangingMesher::mapQuarterTurns(0), 0);
        QCOMPARE(QcHangingMesher::mapQuarterTurns(1), 1);
        QCOMPARE(QcHangingMesher::mapQuarterTurns(3), 3);
        QCOMPARE(QcHangingMesher::mapQuarterTurns(4), 0);
        QCOMPARE(QcHangingMesher::mapQuarterTurns(7), 3);
    }

    void theMapCoversTheWholeFaceAndTurnsClockwise()
    {
        const QRectF uv(0.25, 0.5, 0.125, 0.125);
        QcEntityMesh upright;
        QcHangingMesher::appendFramedMap(upright, southFrame(0), uv, {1.0f, 0.0f});
        QCOMPARE(upright.vertices.size(), qsizetype(4));
        QCOMPARE(upright.indices.size(), qsizetype(6));
        // Over the whole face, standing out from the wall like the frame.
        for (const QcEntityVertex &vertex : std::as_const(upright.vertices)) {
            QVERIFY(vertex.x == 4.0f || vertex.x == 5.0f);
            QVERIFY(vertex.y == 64.0f || vertex.y == 65.0f);
            QCOMPARE(vertex.z, float(7.0 + QcHanging::frameDepth));
        }
        // Seen from the south, right is east: the picture's top left corner is the upper west one.
        QCOMPARE(uvAt(upright, 4.0, 65.0), uv.topLeft());
        QCOMPARE(uvAt(upright, 5.0, 65.0), uv.topRight());
        QCOMPARE(uvAt(upright, 5.0, 64.0), uv.bottomRight());

        // A quarter turn clockwise: the picture's top left corner goes to the top right.
        QcEntityMesh turned;
        QcHangingMesher::appendFramedMap(turned, southFrame(1), uv, {1.0f, 0.0f});
        QCOMPARE(uvAt(turned, 5.0, 65.0), uv.topLeft());
        QCOMPARE(uvAt(turned, 5.0, 64.0), uv.topRight());
        QCOMPARE(uvAt(turned, 4.0, 65.0), uv.bottomLeft());

        // The fifth step looks like the first.
        QcEntityMesh again;
        QcHangingMesher::appendFramedMap(again, southFrame(5), uv, {1.0f, 0.0f});
        QCOMPARE(again.vertices.size(), turned.vertices.size());
        for (qsizetype corner = 0; corner < again.vertices.size(); ++corner) {
            const QcEntityVertex a = again.vertices.at(corner);
            const QcEntityVertex b = turned.vertices.at(corner);
            QCOMPARE(a.u, b.u);
            QCOMPARE(a.v, b.v);
        }
    }

    void onlyFramesWithAFilledMapShowOne()
    {
        QcClientEntity frame;
        frame.kind = QcEntityKind::Hanging;
        frame.hanging = southFrame(0);
        frame.held = item("qtcraft:item_frame");
        frame.item = QcItemStack{item("qtcraft:filled_map"), 1, 0}.withMapId(12);
        QCOMPARE(QcHangingMesher::framedMap(frame, *m_content), std::optional<quint32>(12));
        frame.item = QcItemStack{item("qtcraft:apple"), 1, 0}.withMapId(12);
        QVERIFY(!QcHangingMesher::framedMap(frame, *m_content));
        frame.item = {item("qtcraft:map"), 1, 0};
        QVERIFY(!QcHangingMesher::framedMap(frame, *m_content));
        frame.item = {};
        QVERIFY(!QcHangingMesher::framedMap(frame, *m_content));

        // With a map the frame draws only the edges of a board as large as the face.
        frame.item = QcItemStack{item("qtcraft:filled_map"), 1, 0}.withMapId(12);
        const QcEntityMesher::UvLookup uv = [](const QString &) { return QRectF(0.0, 0.0, 0.1, 0.1); };
        QcEntityMesh mesh;
        QcHangingMesher::append(mesh, frame, *m_content, {1.0f, 0.0f}, uv);
        QCOMPARE(mesh.vertices.size(), qsizetype(16));
        for (const QcEntityVertex &vertex : std::as_const(mesh.vertices)) {
            QVERIFY(vertex.x == 4.0f || vertex.x == 5.0f);
        }
    }

    void tilesAreDrawnOncePerChange()
    {
        QcClientMaps maps;
        maps.apply(wholeMap(3, QcMapPixel::make(1, QcMapPixel::Shade::Normal)));
        QcFramedMapAtlas atlas;
        atlas.setColours({qRgb(10, 20, 30), qRgb(200, 100, 50)});
        const std::shared_ptr<const QList<QcFramedMapAtlas::Tile>> blank = atlas.tiles();
        QVERIFY(!blank->at(0).image.isNull());

        // An unknown map shows the paper tile and draws nothing.
        atlas.beginFrame({99, 3}, maps);
        QCOMPARE(atlas.place(99, maps), QcFramedMapAtlas::uvOf(0));
        QCOMPARE(atlas.tiles(), blank);

        // A known map gets a tile of its own, drawn once.
        const QRectF placed = atlas.place(3, maps);
        QVERIFY(placed != QcFramedMapAtlas::uvOf(0));
        const std::shared_ptr<const QList<QcFramedMapAtlas::Tile>> drawn = atlas.tiles();
        QVERIFY(drawn != blank);
        atlas.beginFrame({3}, maps);
        QCOMPARE(atlas.place(3, maps), placed);
        QCOMPARE(atlas.tiles(), drawn);
        const int index = int(placed.x() * QcFramedMapAtlas::columns) + QcFramedMapAtlas::columns * int(placed.y() * QcFramedMapAtlas::columns);
        const QImage image = drawn->at(index).image;
        QCOMPARE(image.size(), QSize(QcMapData::size, QcMapData::size));
        const QImage expected = QcMapColors::render(maps.find(3)->data, {qRgb(10, 20, 30), qRgb(200, 100, 50)});
        QCOMPARE(image.pixel(5, 5), expected.pixel(5, 5));

        // A new picture draws the tile again; markers of players do not, those of frames do.
        QcMsg::MapUpdate markers = wholeMap(3, QcMapPixel::make(1, QcMapPixel::Shade::Normal));
        markers.area = {};
        markers.pixels.clear();
        markers.markers = {QcMapMarker{0, 0, 0, true, true}};
        maps.apply(markers);
        atlas.beginFrame({3}, maps);
        QCOMPARE(atlas.place(3, maps), placed);
        QCOMPARE(atlas.tiles(), drawn);
        QcMapMarker frame{0, 0, 0, true, false};
        frame.frame = true;
        markers.markers = {frame};
        maps.apply(markers);
        QCOMPARE(atlas.place(3, maps), placed);
        QVERIFY(atlas.tiles() != drawn);
        QVERIFY(atlas.tiles()->at(index).version > drawn->at(index).version);
    }

    void tilesGoToTheNearestMaps()
    {
        QcClientMaps maps;
        QList<quint32> nearestFirst;
        for (quint32 id = 1; id <= quint32(QcFramedMapAtlas::tileCount); ++id) {
            maps.apply(wholeMap(id, QcMapPixel::make(0, QcMapPixel::Shade::Normal)));
            nearestFirst.append(id);
        }
        QcFramedMapAtlas atlas;
        atlas.beginFrame(nearestFirst, maps);
        std::set<std::pair<double, double>> used;
        for (quint32 id = 1; id < quint32(QcFramedMapAtlas::tileCount); ++id) {
            const QPointF corner = atlas.place(id, maps).topLeft();
            used.insert({corner.x(), corner.y()});
        }
        QCOMPARE(used.size(), std::size_t(QcFramedMapAtlas::tileCount - 1));
        const QPointF paper = QcFramedMapAtlas::uvOf(0).topLeft();
        QVERIFY(!used.contains({paper.x(), paper.y()}));
        // Every tile is in use: the farthest map shows paper, wherever it is placed.
        const quint32 last = QcFramedMapAtlas::tileCount;
        QCOMPARE(atlas.place(last, maps), QcFramedMapAtlas::uvOf(0));

        // The farthest map comes nearest: it takes the tile of the map now farthest, the others
        // keep theirs.
        const QRectF second = atlas.place(2, maps);
        const QRectF dropped = atlas.place(last - 1, maps);
        nearestFirst.removeLast();
        nearestFirst.prepend(last);
        atlas.beginFrame(nearestFirst, maps);
        QCOMPARE(atlas.place(last, maps), dropped);
        QCOMPARE(atlas.place(2, maps), second);
        QCOMPARE(atlas.place(last - 1, maps), QcFramedMapAtlas::uvOf(0));
    }

    void aMapNewInTheFrameTakesTheTileUnusedLongest()
    {
        QcClientMaps maps;
        for (quint32 id = 1; id <= 3; ++id) {
            maps.apply(wholeMap(id, QcMapPixel::make(0, QcMapPixel::Shade::Normal)));
        }
        QcFramedMapAtlas atlas;
        atlas.beginFrame({1, 2}, maps);
        const QRectF first = atlas.place(1, maps);
        const QRectF second = atlas.place(2, maps);
        atlas.beginFrame({2}, maps);
        QCOMPARE(atlas.place(2, maps), second);
        // Map 3 takes a tile never used, map 1 keeps its tile until then.
        atlas.beginFrame({3, 1}, maps);
        QCOMPARE(atlas.place(1, maps), first);
        const QRectF third = atlas.place(3, maps);
        QVERIFY(third != first);
        QVERIFY(third != second);
        QVERIFY(third != QcFramedMapAtlas::uvOf(0));
    }
};

QTEST_GUILESS_MAIN(TestFramedMaps)
#include "test_framed_maps.moc"
