#include "app/controllers/QcHeldMap.h"
#include "app/models/QcInventoryModel.h"
#include "app/models/QcMapImageProvider.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/maps/QcMapData.h"

#include <QTest>
#include <QVariantMap>

using namespace Qt::StringLiterals;

// What the HUD shows of a held map (decisions E-803, E-805): the markers as QML reads them, the
// image provider and the tooltip of a filled map.
class TestMapHud : public QObject
{
    Q_OBJECT

private slots:
    void markersReachQmlAsEdgesAndDegrees()
    {
        const QVariantList list = QcHeldMap::markerList({QcMapMarker{-128, 64, 4, true, true}, QcMapMarker{127, 0, 0, false, false}});
        QCOMPARE(list.size(), qsizetype(2));
        const QVariantMap own = list.constFirst().toMap();
        QCOMPARE(own.value(u"x"_s).toDouble(), -1.0);
        QCOMPARE(own.value(u"z"_s).toDouble(), 0.5);
        QCOMPARE(own.value(u"rotation"_s).toDouble(), 90.0);
        QCOMPARE(own.value(u"onMap"_s).toBool(), true);
        QCOMPARE(own.value(u"own"_s).toBool(), true);
        const QVariantMap other = list.constLast().toMap();
        QCOMPARE(other.value(u"onMap"_s).toBool(), false);
        QCOMPARE(other.value(u"own"_s).toBool(), false);
    }

    void providerServesTheLatestImageOfAMap()
    {
        QcMapImageProvider provider;
        QImage red(QcMapData::size, QcMapData::size, QImage::Format_ARGB32);
        red.fill(Qt::red);
        provider.setImage(3, red);
        QSize size;
        const QImage served = provider.requestImage(u"3/17-1"_s, &size, {});
        QCOMPARE(size, QSize(QcMapData::size, QcMapData::size));
        QCOMPARE(served.pixel(5, 5), QColor(Qt::red).rgb());
        // A map not drawn yet: transparent, the paper shows.
        const QImage unknown = provider.requestImage(u"4/1-1"_s, nullptr, {});
        QCOMPARE(qAlpha(unknown.pixel(0, 0)), 0);
        // Only a few images are kept.
        for (quint32 id = 10; id < 10 + static_cast<quint32>(QcMapImageProvider::maxImages); ++id) {
            provider.setImage(id, red);
        }
        const QImage dropped = provider.requestImage(u"3/17-1"_s, nullptr, {});
        QCOMPARE(qAlpha(dropped.pixel(0, 0)), 0);
    }

    void tooltipNamesTheMapAndItsScale()
    {
        QcContentRegistry content;
        QcItemDefinition map;
        map.name = u"t:filled_map"_s;
        map.map = QcMapItem::Filled;
        const QcItemId id = *content.registerItem(map);
        const QcItemStack stack = QcItemStack{id, 1}.withMapId(12);

        QCOMPARE(QcInventoryModel::tooltipOf(content, stack, u"Map"_s), u"Map\nMap #12"_s);
        QCOMPARE(QcInventoryModel::tooltipOf(content, stack, u"Map"_s, 2), u"Map\nMap #12\nScale 1:4"_s);
    }
};

QTEST_GUILESS_MAIN(TestMapHud)
#include "test_map_hud.moc"
