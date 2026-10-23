#include "clientcore/maps/QcClientMaps.h"
#include "clientcore/maps/QcMapColors.h"
#include "clientcore/session/QcClientLauncher.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/maps/QcMapPixel.h"

#include <QBuffer>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

using Shade = QcMapPixel::Shade;

QcMsg::MapUpdate update(quint32 id, int scale, QPoint centre, QRect area, quint16 value)
{
    QcMsg::MapUpdate made;
    made.id = id;
    made.scale = static_cast<quint8>(scale);
    made.centre = centre;
    made.area = area;
    made.pixels = QList<quint16>(static_cast<qsizetype>(area.width()) * area.height(), value);
    return made;
}

QImage filled(int size, const QColor &colour)
{
    QImage image(size, size, QImage::Format_RGBA8888);
    image.fill(colour);
    return image;
}

QByteArray png(const QImage &image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

QcBlockDefinition block(const QString &name, const QString &top, const QString &sides)
{
    QcBlockDefinition made;
    made.name = name;
    made.renderLayer = QcRenderLayer::Opaque;
    made.opaque = true;
    made.faceTextures.fill(sides);
    made.faceTextures[static_cast<std::size_t>(QcBlockFace::Up)] = top;
    return made;
}

// The block definition index of the named block (as map pixels name blocks).
quint32 indexOf(const QcContentRegistry &content, const QString &name)
{
    return content.state(*content.blockId(name)).type;
}

} // namespace

// The client side of maps (decisions E-801, E-802, E-804): the cache of the server's updates, the
// map colours from the textures as drawn, the map image, and using an empty map.
class TestClientMaps : public QObject
{
    Q_OBJECT

private slots:
    void firstUpdateStartsABlankMapWithItsArea()
    {
        QcClientMaps maps;
        QVERIFY(!maps.find(7));
        QcMsg::MapUpdate first = update(7, 1, {64, -128}, {10, 20, 3, 2}, QcMapPixel::make(5, Shade::Bright));
        first.markers = {QcMapMarker{-128, 127, 4, true, true}};
        maps.apply(first);

        const QcClientMaps::Map *map = maps.find(7);
        QVERIFY(map);
        QCOMPARE(map->data.scale(), 1);
        QCOMPARE(map->data.centre(), QPoint(64, -128));
        QCOMPARE(map->data.revealedCount(), 6);
        QCOMPARE(map->data.at({12, 21}), QcMapPixel::make(5, Shade::Bright));
        QCOMPARE(map->data.at({13, 21}), QcMapPixel::blank);
        QCOMPARE(map->markers, first.markers);
        QVERIFY(map->revision > 0);
    }

    void laterUpdatesChangeOnlyTheirArea()
    {
        QcClientMaps maps;
        maps.apply(update(1, 0, {0, 0}, {0, 0, 4, 4}, QcMapPixel::make(2, Shade::Normal)));
        const quint64 before = maps.find(1)->revision;
        maps.apply(update(1, 0, {0, 0}, {2, 2, 4, 4}, QcMapPixel::make(3, Shade::Dark)));

        const QcClientMaps::Map *map = maps.find(1);
        QCOMPARE(map->data.at({0, 0}), QcMapPixel::make(2, Shade::Normal));
        QCOMPARE(map->data.at({3, 3}), QcMapPixel::make(3, Shade::Dark));
        QCOMPARE(map->data.at({5, 5}), QcMapPixel::make(3, Shade::Dark));
        QCOMPARE(map->data.revealedCount(), 16 + 16 - 4);
        QVERIFY(map->revision > before);
        QCOMPARE(map->pictureRevision, map->revision);
        // Markers only: an empty area keeps the pixels, the markers are replaced.
        QcMsg::MapUpdate markersOnly = update(1, 0, {0, 0}, {}, 0);
        maps.apply(markersOnly);
        QCOMPARE(maps.find(1)->data.revealedCount(), 28);
        QVERIFY(maps.find(1)->markers.isEmpty());
        // The picture did not change with the markers.
        QVERIFY(maps.find(1)->revision > maps.find(1)->pictureRevision);
    }

    void anotherScaleOrCentreStartsAgain()
    {
        QcClientMaps maps;
        maps.apply(update(1, 0, {0, 0}, {0, 0, 8, 8}, QcMapPixel::make(2, Shade::Normal)));
        maps.apply(update(1, 1, {0, 0}, {0, 0, 1, 1}, QcMapPixel::make(2, Shade::Normal)));
        QCOMPARE(maps.find(1)->data.scale(), 1);
        QCOMPARE(maps.find(1)->data.revealedCount(), 1);
        maps.apply(update(1, 1, {256, 0}, {0, 0, 2, 1}, QcMapPixel::make(2, Shade::Normal)));
        QCOMPARE(maps.find(1)->data.centre(), QPoint(256, 0));
        QCOMPARE(maps.find(1)->data.revealedCount(), 2);
    }

    void brokenUpdatesDoNoHarm()
    {
        QcClientMaps maps;
        // A scale out of range is no map at all.
        maps.apply(update(1, 9, {0, 0}, {0, 0, 1, 1}, 4));
        QVERIFY(!maps.find(1));
        // Pixels that do not fit their area are left out, the rest of the update applies.
        QcMsg::MapUpdate wrong = update(2, 0, {0, 0}, {120, 0, 16, 1}, 4);
        wrong.markers = {QcMapMarker{}};
        maps.apply(wrong);
        QVERIFY(maps.find(2));
        QCOMPARE(maps.find(2)->data.revealedCount(), 0);
        QCOMPARE(maps.find(2)->markers.size(), qsizetype(1));
        // More markers than a map shows are cut.
        QcMsg::MapUpdate crowded = update(3, 0, {0, 0}, {}, 0);
        crowded.markers = QList<QcMapMarker>(QcMapMarker::maxCount + 5);
        maps.apply(crowded);
        QCOMPARE(maps.find(3)->markers.size(), qsizetype(QcMapMarker::maxCount));
    }

    void forgetsTheMapUpdatedLongestAgo()
    {
        QcClientMaps maps;
        for (quint32 id = 0; id < static_cast<quint32>(QcClientMaps::maxMaps); ++id) {
            maps.apply(update(id, 0, {0, 0}, {}, 0));
        }
        // Map 0 is updated again, so map 1 is the oldest.
        maps.apply(update(0, 0, {0, 0}, {}, 0));
        maps.apply(update(5000, 0, {0, 0}, {}, 0));
        QCOMPARE(maps.count(), qsizetype(QcClientMaps::maxMaps));
        QVERIFY(maps.find(0));
        QVERIFY(!maps.find(1));
        QVERIFY(maps.find(2));
        QVERIFY(maps.find(5000));
    }

    void averageWeighsTexelsByAlpha()
    {
        QImage half(2, 1, QImage::Format_ARGB32);
        half.setPixel(0, 0, qRgba(200, 0, 0, 255));
        half.setPixel(1, 0, qRgba(0, 0, 200, 0));
        QCOMPARE(QcMapColors::average(half), qRgb(200, 0, 0));
        QCOMPARE(QcMapColors::average(filled(4, QColor(10, 20, 30))), qRgb(10, 20, 30));
        QCOMPARE(QcMapColors::average(filled(4, Qt::transparent)), QcMapColors::unknown);
        QCOMPARE(QcMapColors::average(QImage()), QcMapColors::unknown);
    }

    void coloursFollowTheTopTextureAndThePacks()
    {
        QcContentRegistry content;
        content.assets().registerTexture({u"t:grass_top"_s, {}, png(filled(16, QColor(200, 0, 0)))});
        content.assets().registerTexture({u"t:grass_side"_s, {}, png(filled(16, QColor(0, 0, 200)))});
        QVERIFY(content.registerBlock(block(u"t:grass"_s, u"t:grass_top"_s, u"t:grass_side"_s)));
        QVERIFY(content.registerBlock(block(u"t:odd"_s, u"t:absent"_s, u"t:absent"_s)));
        const quint32 grass = indexOf(content, u"t:grass"_s);
        const quint32 odd = indexOf(content, u"t:odd"_s);

        const QList<QRgb> plain = QcMapColors::ofAtlas(content, QcTextureAtlas::build(content));
        QCOMPARE(plain.size(), content.blocks().size());
        QCOMPARE(plain.value(grass), qRgb(200, 0, 0));
        // No texture: neutral grey.
        QCOMPARE(plain.value(odd), QcMapColors::unknown);

        // A texture pack changes the map colours with the texture.
        auto pack = std::make_shared<QcTexturePack>();
        pack->id = u"user:green"_s;
        pack->manifest.name = u"green"_s;
        pack->manifest.resolution = 16;
        pack->images.insert(u"t:grass_top"_s, filled(16, QColor(0, 180, 0)));
        const QList<QRgb> packed = QcMapColors::ofAtlas(content, QcTextureAtlas::build(content, {pack}));
        QCOMPARE(packed.value(grass), qRgb(0, 180, 0));
    }

    void imageShowsPaperWhereUnexploredAndShadesTheRest()
    {
        QcMapData map = *QcMapData::fromParts(0, {0, 0}, QList<quint16>(QcMapData::pixelCount, 0));
        map.set({0, 0}, QcMapPixel::make(0, Shade::Bright));
        map.set({1, 0}, QcMapPixel::make(0, Shade::Normal));
        map.set({2, 0}, QcMapPixel::make(0, Shade::Dark));
        // A block the client does not know (untrusted data): grey, not a crash.
        map.set({3, 0}, QcMapPixel::make(900, Shade::Bright));
        const QList<QRgb> colours{qRgb(255, 100, 51)};

        const QImage image = QcMapColors::render(map, colours);
        QCOMPARE(image.size(), QSize(QcMapData::size, QcMapData::size));
        QCOMPARE(image.pixel(0, 0), qRgb(255, 100, 51));
        QCOMPARE(image.pixel(1, 0), qRgb(220, 86, 44));
        QCOMPARE(image.pixel(2, 0), qRgb(180, 70, 36));
        QCOMPARE(image.pixel(3, 0), QcMapColors::unknown);
        QCOMPARE(qAlpha(image.pixel(4, 0)), 0);
        QCOMPARE(qAlpha(image.pixel(127, 127)), 0);
    }

    void anEmptyMapIsUsedWhereverThePlayerAims()
    {
        QcItemDefinition empty;
        empty.map = QcMapItem::Empty;
        QVERIFY(QcClientLauncher::consumes(empty, true));
        QVERIFY(QcClientLauncher::consumes(empty, false));
        QcItemDefinition filledMap;
        filledMap.map = QcMapItem::Filled;
        QVERIFY(!QcClientLauncher::consumes(filledMap, false));
    }
};

QTEST_GUILESS_MAIN(TestClientMaps)
#include "test_client_maps.moc"
