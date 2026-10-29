// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Item icons in QML's pixmap cache (decision E-650): after new icons (texture pack switch, other
// content) an image of an item shows the new icon, also one created after the switch with the same
// item and size, and an icon asked for before the content arrived does not stay empty.
#include "app/models/QcItemIconProvider.h"
#include "app/models/QcItemIconUrls.h"

#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

QHash<QString, QImage> iconsOf(QColor colour)
{
    QImage icon(QcItemIconProvider::iconSize, QcItemIconProvider::iconSize, QImage::Format_RGBA8888);
    icon.fill(colour);
    return {{u"qtcraft:stone"_s, icon}};
}

// Two images of the same item and size; the second exists only while `second` is true, so it is
// created anew like the slots of a screen that opens.
constexpr auto scene = R"(
import QtQuick
import IconTest

Item {
    width: 64
    height: 32
    property bool second: false

    Image {
        x: 0
        width: 32
        height: 32
        smooth: false
        sourceSize: Qt.size(32, 32)
        source: ItemIcons.source("qtcraft:stone", ItemIcons.generation)
    }
    Loader {
        x: 32
        width: 32
        height: 32
        active: parent.second
        sourceComponent: Image {
            smooth: false
            sourceSize: Qt.size(32, 32)
            source: ItemIcons.source("qtcraft:stone", ItemIcons.generation)
        }
    }
}
)";

} // namespace

class TestItemIconCache : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void urlCarriesGeneration();
    void providerIgnoresQuery();
    void newIconsReachCachedImages();

private:
    QColor pixelAt(QQuickView &view, QPoint point);
};

void TestItemIconCache::initTestCase()
{
    qmlRegisterSingletonInstance("IconTest", 1, 0, "ItemIcons", QcItemIconUrls::instance());
}

void TestItemIconCache::urlCarriesGeneration()
{
    const int before = QcItemIconUrls::instance()->generation();
    QCOMPARE(QcItemIconUrls::url(u"qtcraft:stone"_s), u"image://items/qtcraft:stone?g=%1"_s.arg(before));
    QCOMPARE(QcItemIconUrls::url(QString()), QString());

    QcItemIconProvider provider;
    QSignalSpy changed(QcItemIconUrls::instance(), &QcItemIconUrls::generationChanged);
    provider.setIcons(iconsOf(Qt::red));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(QcItemIconUrls::instance()->generation(), before + 1);
}

void TestItemIconCache::providerIgnoresQuery()
{
    QcItemIconProvider provider;
    provider.setIcons(iconsOf(Qt::red));
    QSize size;
    const QImage icon = provider.requestImage(u"qtcraft:stone?g=7"_s, &size, QSize(32, 32));
    QCOMPARE(size, QSize(QcItemIconProvider::iconSize, QcItemIconProvider::iconSize));
    QCOMPARE(icon.size(), QSize(32, 32));
    QCOMPARE(icon.pixelColor(31, 31), QColor(Qt::red));

    // The scaled image is kept per size: a second request gives the same pixels.
    const QImage again = provider.requestImage(u"qtcraft:stone?g=8"_s, &size, QSize(32, 32));
    QCOMPARE(again, icon);
}

QColor TestItemIconCache::pixelAt(QQuickView &view, QPoint point)
{
    QCoreApplication::processEvents();
    return view.grabWindow().pixelColor(point);
}

void TestItemIconCache::newIconsReachCachedImages()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile file(dir.filePath(u"Scene.qml"_s));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(scene);
    file.close();

    QQuickView view;
    auto *provider = new QcItemIconProvider;
    view.engine()->addImageProvider(QString::fromLatin1(QcItemIconProvider::providerId), provider);
    view.setColor(Qt::black);
    view.setSource(QUrl::fromLocalFile(file.fileName()));
    QCOMPARE(view.status(), QQuickView::Ready);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    // Asked for before the content arrived: transparent, the black background shows.
    QTRY_COMPARE(pixelAt(view, {16, 16}), QColor(Qt::black));

    provider->setIcons(iconsOf(Qt::red));
    QTRY_COMPARE(pixelAt(view, {16, 16}), QColor(Qt::red));

    // New icons (a texture pack switch): the shown image and a newly created one show them.
    provider->setIcons(iconsOf(Qt::blue));
    view.rootObject()->setProperty("second", true);
    QTRY_COMPARE(pixelAt(view, {48, 16}), QColor(Qt::blue));
    QTRY_COMPARE(pixelAt(view, {16, 16}), QColor(Qt::blue));
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QGuiApplication app(argc, argv);
    TestItemIconCache test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_item_icon_cache.moc"
