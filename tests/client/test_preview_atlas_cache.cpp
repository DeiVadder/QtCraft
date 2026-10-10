#include "QcPreviewAtlasCache.h"

#include <QTest>

#include <memory>

// The character previews share one atlas texture per image and QRhi (decision E-231); with the last
// preview the texture goes. Runs on the Null backend of QRhi, no GPU needed.
class TestPreviewAtlasCache : public QObject
{
    Q_OBJECT

private slots:
    void previewsShareOneTexture()
    {
        QRhiNullInitParams params;
        const std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::Null, &params));
        QVERIFY(rhi);
        QImage atlas(64, 64, QImage::Format_RGBA8888);
        atlas.fill(Qt::red);
        QCOMPARE(QcPreviewAtlasCache::liveTextures(), 0);

        QRhiResourceUpdateBatch *updates = rhi->nextResourceUpdateBatch();
        // Three previews with copies of the same atlas image: one texture.
        const QImage copy = atlas;
        std::shared_ptr<QRhiTexture> menu = QcPreviewAtlasCache::texture(rhi.get(), atlas, updates);
        std::shared_ptr<QRhiTexture> screen = QcPreviewAtlasCache::texture(rhi.get(), copy, updates);
        std::shared_ptr<QRhiTexture> inventory = QcPreviewAtlasCache::texture(rhi.get(), atlas, updates);
        QVERIFY(menu);
        QCOMPARE(screen, menu);
        QCOMPARE(inventory, menu);
        QCOMPARE(menu->pixelSize(), QSize(64, 64));
        QCOMPARE(QcPreviewAtlasCache::liveTextures(), 1);

        // Another atlas (mods switched) is a texture of its own.
        QImage other(32, 32, QImage::Format_RGBA8888);
        other.fill(Qt::blue);
        std::shared_ptr<QRhiTexture> world = QcPreviewAtlasCache::texture(rhi.get(), other, updates);
        QVERIFY(world != menu);
        QCOMPARE(QcPreviewAtlasCache::liveTextures(), 2);

        // Hidden previews let go; the texture goes with the last of them.
        menu.reset();
        screen.reset();
        QCOMPARE(QcPreviewAtlasCache::liveTextures(), 2);
        inventory.reset();
        QCOMPARE(QcPreviewAtlasCache::liveTextures(), 1);
        world.reset();
        QCOMPARE(QcPreviewAtlasCache::liveTextures(), 0);
        updates->release();
    }
};

QTEST_GUILESS_MAIN(TestPreviewAtlasCache)
#include "test_preview_atlas_cache.moc"
