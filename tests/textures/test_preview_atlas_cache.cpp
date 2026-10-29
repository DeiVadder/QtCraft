// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/preview/QcPreviewAtlasCache.h"
#include "app/rendering/QcAtlasTexture.h"

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
        std::shared_ptr<QRhiTexture> menu = QcPreviewAtlasCache::texture(rhi.get(), {atlas}, updates);
        std::shared_ptr<QRhiTexture> screen = QcPreviewAtlasCache::texture(rhi.get(), {copy}, updates);
        std::shared_ptr<QRhiTexture> inventory = QcPreviewAtlasCache::texture(rhi.get(), {atlas}, updates);
        QVERIFY(menu);
        QCOMPARE(screen, menu);
        QCOMPARE(inventory, menu);
        QCOMPARE(menu->pixelSize(), QSize(64, 64));
        QCOMPARE(QcPreviewAtlasCache::liveTextures(), 1);

        // Another atlas (mods switched) is a texture of its own.
        QImage other(32, 32, QImage::Format_RGBA8888);
        other.fill(Qt::blue);
        std::shared_ptr<QRhiTexture> world = QcPreviewAtlasCache::texture(rhi.get(), {other}, updates);
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

    // An atlas larger than the GPU takes starts at the first mip level that fits (decision E-300);
    // below the atlas's own levels the chain is halved down to 1 x 1, as QRhi expects.
    void tooLargeAtlasStartsAtALowerLevel()
    {
        const auto level = [](int width, int height) {
            QImage image(width, height, QImage::Format_RGBA8888);
            image.fill(Qt::green);
            return image;
        };
        const QList<QImage> levels = {level(80, 40), level(40, 20), level(20, 10)};

        const QList<QImage> full = QcAtlasTexture::chain(levels, 4096);
        QCOMPARE(full.front().size(), QSize(80, 40));
        QCOMPARE(full.size(), 7);
        QCOMPARE(full.back().size(), QSize(1, 1));
        QCOMPARE(full[3].size(), QSize(10, 5));
        QCOMPARE(full[4].size(), QSize(5, 2));

        const QList<QImage> limited = QcAtlasTexture::chain(levels, 64);
        QCOMPARE(limited.front().size(), QSize(40, 20));
        QCOMPARE(limited.size(), 6);
        QCOMPARE(limited.back().pixelColor(0, 0), QColor(Qt::green));

        QRhiNullInitParams params;
        const std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::Null, &params));
        QRhiResourceUpdateBatch *updates = rhi->nextResourceUpdateBatch();
        const std::unique_ptr<QRhiTexture> texture = QcAtlasTexture::create(rhi.get(), levels, updates);
        QVERIFY(texture);
        QVERIFY(texture->flags().testFlag(QRhiTexture::MipMapped));
        QCOMPARE(texture->pixelSize(), QSize(80, 40));
        updates->release();
    }

    // When the GPU refuses a texture (out of memory), the next smaller level is tried (S1 of the
    // stage 28 review); none at all gives no level, and the caller falls back to the placeholder.
    void refusedLevelsFallBackToSmallerOnes()
    {
        const auto level = [](int width, int height) {
            QImage image(width, height, QImage::Format_RGBA8888);
            image.fill(Qt::green);
            return image;
        };
        const QList<QImage> levels = {level(80, 40), level(40, 20), level(20, 10)};
        QList<QSize> tried;
        const auto refuseLarge = [&tried](QSize size) {
            tried.append(size);
            return size.width() <= 20;
        };
        QCOMPARE(QcAtlasTexture::usableLevel(levels, 4096, refuseLarge), std::optional<qsizetype>(2));
        QCOMPARE(tried, (QList<QSize>{{80, 40}, {40, 20}, {20, 10}}));
        // Levels above the GPU limit are not even tried.
        tried.clear();
        QCOMPARE(QcAtlasTexture::usableLevel(levels, 50, refuseLarge), std::optional<qsizetype>(2));
        QCOMPARE(tried.front(), QSize(40, 20));
        QVERIFY(!QcAtlasTexture::usableLevel(levels, 4096, [](QSize) { return false; }).has_value());
        QCOMPARE(QcAtlasTexture::placeholder().pixelColor(3, 3), QColor(Qt::magenta));
    }
};

QTEST_GUILESS_MAIN(TestPreviewAtlasCache)
#include "test_preview_atlas_cache.moc"
