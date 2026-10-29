// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/realistic/QcMaterialTextures.h"
#include "app/realistic/QcShadowMap.h"
#include "app/rendering/QcRenderPipelines.h"
#include "app/rendering/QcWorldRendererResources.h"
#include "clientcore/maps/QcFramedMapAtlas.h"
#include "clientcore/materials/QcMaterialAtlas.h"
#include "clientcore/settings/QcRenderOptions.h"
#include "clientcore/textures/QcAtlasLayout.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/registry/QcContentRegistry.h"
#include "fixtures/QcTestUtil.h"

#include <QImage>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QTest>
#include <rhi/qrhi.h>

#include <memory>

using namespace Qt::StringLiterals;

// The material maps of the realistic mode on QRhi's Null backend (decision E-908): flat textures
// stand in while there are no maps (quality Low, no maps built), the maps replace them only when
// wanted, release() and the classic mode leave nothing on the GPU, and the receivers' bindings
// with the maps fit the entity pipeline built with them together with the framed maps' bindings
// (decision E-896) whichever textures stand at 5 and 6. The receiver shaders are baked into the
// test (tests/client/tests.cmake). The pixels of a set are let go once uploaded, refused or not
// drawn any more; a set whose pixels are gone is reported lost once when it is wanted again; maps
// whose cells differ from the colour atlas's are not bound (decision E-912).
class TestMaterialTextures : public QObject
{
    Q_OBJECT

    std::unique_ptr<QRhi> m_rhi;
    std::shared_ptr<const QcMaterialAtlas> m_maps;

    static std::shared_ptr<const QcMaterialAtlas> maps()
    {
        const auto base = QcTestUtil::baseContent();
        const QcContentRegistry &content = *base->registry;
        return std::make_shared<const QcMaterialAtlas>(QcMaterialAtlas::build(QcTextureAtlas::build(content), content, {}));
    }

    QcWorldDeviceResources device(QRhiResourceUpdateBatch *updates) const
    {
        QcWorldDeviceResources result = QcWorldDeviceResources::create(m_rhi.get());
        QImage atlas(10, 10, QImage::Format_RGBA8888);
        atlas.fill(Qt::gray);
        QImage animations(2, 2, QImage::Format_RGBA8888);
        animations.fill(Qt::transparent);
        result.replaceAtlas({atlas}, animations, 1, updates);
        return result;
    }

    // Prepares frame after frame as the renderer does, until the frame whose replaces() is true
    // binds the maps: that frame's number, -1 when none does within 64 frames or the maps are bound
    // on another frame.
    int framesUntilBound(QcMaterialTextures &materials, QRhiResourceUpdateBatch *updates) const
    {
        for (int frame = 1; frame <= 64; ++frame) {
            const bool replacing = materials.replaces(m_rhi.get(), true);
            if (!materials.prepare(m_rhi.get(), true, updates) || replacing != materials.hasMaps()) {
                return -1;
            }
            if (replacing) {
                return frame;
            }
        }
        return -1;
    }

    // Prepares frame after frame on `rhi` until the frame whose replaces() is true: that frame's
    // number; -1 when none does within 64 frames or the bound entries change before.
    static int framesUntilReplaced(QcMaterialTextures &materials, QRhi *rhi, QRhiResourceUpdateBatch *updates)
    {
        const QList<QRhiShaderResourceBinding> before = materials.entries();
        for (int frame = 1; frame <= 64; ++frame) {
            const bool replacing = materials.replaces(rhi, true);
            if (!materials.prepare(rhi, true, updates)) {
                return -1;
            }
            if (replacing) {
                return frame;
            }
            if (materials.entries() != before) {
                return -1;
            }
        }
        return -1;
    }

    // Frames a whole upload of `materials`' maps takes at least (a frame may take one row more
    // than its budget).
    static int streamedFrames(const QcMaterialTextures &materials)
    {
        return static_cast<int>(materials.bytes() / (materials.uploadBudget() + materials.uploadBudget() / 10));
    }

    // Small steps, so the small maps of the base content stream over many frames.
    static constexpr qint64 smallSteps = qint64{256} * 1024;

private slots:
    void initTestCase()
    {
        QRhiNullInitParams params;
        m_rhi.reset(QRhi::create(QRhi::Null, &params));
        QVERIFY(m_rhi);
        m_maps = maps();
        QVERIFY(m_maps);
    }

    void flatUntilMapsAreWanted()
    {
        QcMaterialTextures materials;
        QVERIFY(!materials.holdsResources());
        QVERIFY(materials.entries().isEmpty());
        QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();
        materials.offer(m_maps->serial(), m_maps);

        // Quality Low: the flat textures only; the pixels wait for their upload.
        QVERIFY(materials.prepare(m_rhi.get(), false, updates));
        QVERIFY(materials.holdsResources());
        QVERIFY(!materials.hasMaps());
        QVERIFY(materials.holdsPixels());
        QCOMPARE(materials.bytes(), 0);
        const QList<QRhiShaderResourceBinding> flat = materials.entries();
        QCOMPARE(flat.size(), 2);
        QVERIFY(!materials.replaces(m_rhi.get(), false));
        QVERIFY(!materials.takeLost(true));

        // Medium: the maps, uploaded a few MiB per frame and bound (replaces() says so before) on
        // the frame after the last rows; then their pixels go. The same set again changes nothing.
        const int frames = framesUntilBound(materials, updates);
        const int streamed = static_cast<int>((materials.bytes() + QcMaterialTextures::uploadBytesPerFrame - 1) / QcMaterialTextures::uploadBytesPerFrame);
        QVERIFY2(frames >= streamed + 1 && frames <= streamed + 2, qPrintable(u"%1 frames for %2 bytes"_s.arg(frames).arg(materials.bytes())));
        QVERIFY(frames >= 2);
        QVERIFY(materials.hasMaps());
        QVERIFY(!materials.holdsPixels());
        // The GPU has the levels down to 1 x 1 too.
        QVERIFY(materials.bytes() >= m_maps->bytes() && materials.bytes() < m_maps->bytes() * 11 / 10);
        materials.offer(m_maps->serial(), nullptr);
        QVERIFY(!materials.replaces(m_rhi.get(), true));
        QVERIFY(!materials.takeLost(true));
        QVERIFY(materials.entries() != flat);
        // Its pixels handed over again: nothing to upload, and not kept.
        materials.offer(m_maps->serial(), m_maps);
        QVERIFY(!materials.replaces(m_rhi.get(), true));
        QVERIFY(materials.prepare(m_rhi.get(), true, updates));
        QVERIFY(materials.hasMaps());
        QVERIFY(!materials.holdsPixels());

        // Back to Low: the maps go. Wanted again without a new set, they are lost (reported once)
        // and the flat textures stand in.
        QVERIFY(materials.replaces(m_rhi.get(), false));
        QVERIFY(materials.prepare(m_rhi.get(), false, updates));
        QVERIFY(!materials.hasMaps());
        QVERIFY(!materials.takeLost(false));
        QVERIFY(materials.takeLost(true));
        QVERIFY(!materials.takeLost(true));
        QVERIFY(!materials.replaces(m_rhi.get(), true));
        QVERIFY(materials.prepare(m_rhi.get(), true, updates));
        QVERIFY(!materials.hasMaps());

        // A new set (built again) is uploaded; no set at all is flat. Then classic: nothing.
        const std::shared_ptr<const QcMaterialAtlas> again = maps();
        materials.offer(again->serial(), again);
        QVERIFY(framesUntilBound(materials, updates) > 0);
        materials.offer(0, nullptr);
        QVERIFY(materials.replaces(m_rhi.get(), true));
        QVERIFY(materials.prepare(m_rhi.get(), true, updates));
        QVERIFY(!materials.hasMaps());
        QVERIFY(!materials.takeLost(true));
        materials.release();
        QVERIFY(!materials.holdsResources());
        QVERIFY(materials.entries().isEmpty());
        updates->release();
    }

    // The renderer keeps no copy of uploaded maps: handed over as the world view does, they are
    // freed with the upload (the batch holds only the images, until it is done; decision E-912).
    void uploadedPixelsAreNotKept()
    {
        std::shared_ptr<const QcMaterialAtlas> handed = maps();
        const std::weak_ptr<const QcMaterialAtlas> watched = handed;
        QcMaterialTextures materials;
        QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();
        const quint64 serial = handed->serial();
        materials.offer(serial, std::move(handed));
        QVERIFY(materials.holdsPixels());
        QVERIFY(!watched.expired());
        QVERIFY(framesUntilBound(materials, updates) > 0);
        QVERIFY(watched.expired());
        updates->release();
    }

    // A new QRhi while a set streams (decision E-912): its pixels stay, the upload starts over on
    // the new QRhi, and nothing is reported lost.
    void aNewDeviceRestartsTheUpload()
    {
        QRhiNullInitParams params;
        const std::unique_ptr<QRhi> other(QRhi::create(QRhi::Null, &params));
        QVERIFY(other);
        QcMaterialTextures materials;
        materials.setUploadBudget(smallSteps);
        QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();
        materials.offer(m_maps->serial(), m_maps);
        QVERIFY(materials.prepare(m_rhi.get(), true, updates));
        QVERIFY(materials.prepare(m_rhi.get(), true, updates));
        QVERIFY(!materials.hasMaps());

        QVERIFY(materials.replaces(other.get(), true));
        QRhiResourceUpdateBatch *otherUpdates = other->nextResourceUpdateBatch();
        QVERIFY(materials.prepare(other.get(), true, otherUpdates));
        QVERIFY(materials.holdsPixels());
        QVERIFY(!materials.hasMaps());
        QVERIFY(!materials.takeLost(true));
        // The whole set again (the first frame on the new QRhi already took its share).
        const int frames = framesUntilReplaced(materials, other.get(), otherUpdates);
        QVERIFY(materials.hasMaps());
        QVERIFY2(frames >= streamedFrames(materials), qPrintable(u"%1 frames"_s.arg(frames)));
        QVERIFY(!materials.holdsPixels());
        QVERIFY(!materials.takeLost(true));
        materials.release();
        otherUpdates->release();
        updates->release();
    }

    // A new set while another streams (a pack switched twice): the bound maps stay until the
    // newest set is on the GPU, the set on its way is dropped and the newest uploaded whole.
    void aNewSetMidStreamKeepsTheBoundMaps()
    {
        QcMaterialTextures materials;
        materials.setUploadBudget(smallSteps);
        QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();
        materials.offer(m_maps->serial(), m_maps);
        QVERIFY(framesUntilBound(materials, updates) > 0);
        const QList<QRhiShaderResourceBinding> bound = materials.entries();

        const std::shared_ptr<const QcMaterialAtlas> second = maps();
        materials.offer(second->serial(), second);
        for (int frame = 0; frame < 2; ++frame) {
            QVERIFY(!materials.replaces(m_rhi.get(), true));
            QVERIFY(materials.prepare(m_rhi.get(), true, updates));
            QCOMPARE(materials.entries(), bound);
        }
        std::shared_ptr<const QcMaterialAtlas> third = maps();
        const std::weak_ptr<const QcMaterialAtlas> watched = third;
        const quint64 serial = third->serial();
        materials.offer(serial, std::move(third));
        const int frames = framesUntilReplaced(materials, m_rhi.get(), updates);
        QVERIFY2(frames >= streamedFrames(materials) + 1, qPrintable(u"%1 frames"_s.arg(frames)));
        QVERIFY(materials.entries() != bound);
        QVERIFY(watched.expired());
        QVERIFY(!materials.takeLost(true));
        updates->release();
    }

    // Maps whose cells differ from the colour atlas's (the old pack's maps under a new atlas while
    // the new set streams, or a set that does not fit) are not bound: the flat textures stand in,
    // and a set that does not fit is refused and its pixels let go. A new QRhi reports it lost, so
    // the game builds it again.
    void mapsOfOtherCellsAreNotBound()
    {
        const int cell = QcAtlasLayout::cellSize(m_maps->tileSize());
        QcMaterialTextures materials;
        materials.setAtlasCell(cell);
        QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();
        materials.offer(m_maps->serial(), m_maps);
        QVERIFY(framesUntilBound(materials, updates) > 0);

        // A colour atlas of twice the tile size arrives with a new set of the old size.
        materials.setAtlasCell(cell * 2);
        std::shared_ptr<const QcMaterialAtlas> handed = maps();
        const std::weak_ptr<const QcMaterialAtlas> watched = handed;
        const quint64 serial = handed->serial();
        materials.offer(serial, std::move(handed));
        QVERIFY(materials.replaces(m_rhi.get(), true));
        QVERIFY(materials.prepare(m_rhi.get(), true, updates));
        QVERIFY(!materials.hasMaps());
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"cells differ from the colour atlas"_s));
        for (int frame = 0; frame < 64 && materials.holdsPixels(); ++frame) {
            QVERIFY(materials.prepare(m_rhi.get(), true, updates));
        }
        QVERIFY(!materials.holdsPixels());
        QVERIFY(watched.expired());
        QVERIFY(!materials.hasMaps());
        QVERIFY(!materials.takeLost(true));
        QVERIFY(!materials.replaces(m_rhi.get(), true));

        materials.reset();
        QVERIFY(materials.takeLost(true));
        QVERIFY(!materials.takeLost(true));
        updates->release();
    }

    // Frames drawn without the realistic plan (decision E-912): a set on its way and its pixels go
    // (reported lost once wanted again); the bound maps stay while the realistic mode is wanted
    // (the classic plan as its fallback), and nothing stays in the classic mode.
    void standByLetsThePixelsGo()
    {
        QcMaterialTextures materials;
        QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();
        materials.offer(m_maps->serial(), m_maps);
        QVERIFY(framesUntilBound(materials, updates) > 0);
        const QList<QRhiShaderResourceBinding> bound = materials.entries();
        const qint64 bytes = materials.bytes();

        // Fallback with the bound set only: it stays, nothing is lost.
        materials.standBy(true);
        QVERIFY(materials.hasMaps());
        QCOMPARE(materials.entries(), bound);
        QVERIFY(!materials.replaces(m_rhi.get(), true));
        QVERIFY(!materials.takeLost(true));

        // Fallback while a new set streams: the bound maps stay, the new set's pixels go.
        std::shared_ptr<const QcMaterialAtlas> handed = maps();
        const std::weak_ptr<const QcMaterialAtlas> watched = handed;
        const quint64 serial = handed->serial();
        materials.offer(serial, std::move(handed));
        QVERIFY(materials.prepare(m_rhi.get(), true, updates));
        materials.standBy(true);
        QVERIFY(watched.expired());
        QVERIFY(!materials.holdsPixels());
        QVERIFY(materials.hasMaps());
        QCOMPARE(materials.bytes(), bytes);
        QVERIFY(materials.takeLost(true));
        QVERIFY(!materials.takeLost(true));

        // The classic mode: nothing stays.
        materials.offer(m_maps->serial(), m_maps);
        materials.standBy(false);
        QVERIFY(!materials.holdsResources());
        QVERIFY(!materials.holdsPixels());
        updates->release();
    }

    // The receivers' bindings with the material entries extend the atlas layout to 7 entries; the
    // entity pipeline built with them draws the framed maps with framedMapBindings() of the same
    // entries, with the flat textures as with the maps.
    void receiverBindingsFitWithTheMaps()
    {
        QRhiCommandBuffer *commands = nullptr;
        QCOMPARE(m_rhi->beginOffscreenFrame(&commands), QRhi::FrameOpSuccess);
        QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();
        QcWorldDeviceResources resources = device(updates);
        QImage map(QcMapData::size, QcMapData::size, QImage::Format_RGBA8888);
        map.fill(Qt::darkGreen);
        QList<QcFramedMapAtlas::Tile> tiles(QcFramedMapAtlas::tileCount);
        tiles[0] = {map, 1};
        QVERIFY(resources.framedMaps.upload(m_rhi.get(), updates, tiles, resources.frameUniforms.get()) > 0);

        QcShadowMap shadows;
        QVERIFY(shadows.prepare(m_rhi.get(), QcRenderOptions{.mode = QcRenderOptions::Mode::Realistic}.shadows()));
        QcMaterialTextures materials;
        materials.offer(m_maps->serial(), m_maps);
        QVERIFY(materials.prepare(m_rhi.get(), false, updates));

        std::unique_ptr<QRhiTexture> colour(m_rhi->newTexture(QRhiTexture::RGBA8, {64, 64}, 1, QRhiTexture::RenderTarget));
        QVERIFY(colour->create());
        std::unique_ptr<QRhiTextureRenderTarget> target(m_rhi->newTextureRenderTarget({QRhiColorAttachment(colour.get())}));
        std::unique_ptr<QRhiRenderPassDescriptor> pass(target->newCompatibleRenderPassDescriptor());
        target->setRenderPassDescriptor(pass.get());
        QVERIFY(target->create());
        const QLoggingCategory category("test");

        for (const bool withMaps : {false, true}) {
            for (int frame = 0; frame < 64 && materials.hasMaps() != withMaps; ++frame) {
                if (materials.replaces(m_rhi.get(), withMaps)) {
                    shadows.releaseAtlasBindings();
                }
                QVERIFY(materials.prepare(m_rhi.get(), withMaps, updates));
            }
            QCOMPARE(materials.hasMaps(), withMaps);
            QRhiShaderResourceBindings *const receivers = shadows.receiverBindings(resources, materials.entries());
            QVERIFY(receivers);
            QCOMPARE(receivers->cbeginBindings() + 7, receivers->cendBindings());
            const std::unique_ptr<QRhiGraphicsPipeline> receiving = QcRenderPipelines::createEntityPipeline(
                m_rhi.get(), target.get(), receivers, QcRenderPipelines::loadShader(u":/shaders/entity_shadow.vert.qsb"_s, category),
                QcRenderPipelines::loadShader(u":/shaders/entity_shadow.frag.qsb"_s, category));
            QVERIFY(receiving);
            QRhiShaderResourceBindings *const framed = shadows.framedMapBindings(resources, materials.entries());
            QVERIFY(framed);
            QVERIFY(receiving->shaderResourceBindings()->isLayoutCompatible(framed));
            // Without the material entries the framed maps would not fit (decision E-896).
            QVERIFY(!receivers->isLayoutCompatible(resources.framedMaps.bindings()));
        }
        shadows.releaseAtlasBindings();
        updates->release();
        QCOMPARE(m_rhi->endOffscreenFrame(), QRhi::FrameOpSuccess);
    }
};

QTEST_GUILESS_MAIN(TestMaterialTextures)
#include "test_material_textures.moc"
