// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/realistic/QcShadowMap.h"
#include "app/rendering/QcRenderPipelines.h"
#include "app/rendering/QcWorldRendererResources.h"
#include "clientcore/maps/QcFramedMapAtlas.h"
#include "clientcore/settings/QcRenderOptions.h"

#include <QImage>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QTest>
#include <rhi/qrhi.h>

#include <limits>
#include <memory>

using namespace Qt::StringLiterals;

// The sun's shadow map of the realistic mode (decision E-903) on QRhi's Null backend: the map
// follows the cascades and resolution of the quality (other settings change no resource), the
// classic mode and release() leave nothing on the GPU, the receivers' bindings extend the atlas
// layout, the casters are made for the atlas and again after a new one, a refused request is not
// tried again every frame, and an upload without a light or with NaN casts nothing.
// The caster shaders are baked into the test (tests/client/tests.cmake).
class TestShadowMap : public QObject
{
    Q_OBJECT

    using Quality = QcRenderOptions::Quality;

    std::unique_ptr<QRhi> m_rhi;

    static QcRenderOptions::Shadows shadowsOf(Quality quality)
    {
        return QcRenderOptions{.mode = QcRenderOptions::Mode::Realistic, .quality = quality}.shadows();
    }

    // Device resources with a one-cell atlas (QcAtlasLayout::gridOf), uploaded into `updates`.
    QcWorldDeviceResources device(QRhiResourceUpdateBatch *updates, quint64 version = 1) const
    {
        QcWorldDeviceResources result = QcWorldDeviceResources::create(m_rhi.get());
        QImage atlas(10, 10, QImage::Format_RGBA8888);
        atlas.fill(Qt::gray);
        QImage animations(2, 2, QImage::Format_RGBA8888);
        animations.fill(Qt::transparent);
        result.replaceAtlas({atlas}, animations, version, updates);
        return result;
    }

    static QcShadowMap::Frame noon()
    {
        QcShadowMap::Frame frame;
        frame.camera = QcVec3{12.5, 70.0, -3.25};
        frame.forward = QVector3D(0.0f, 0.0f, -1.0f);
        frame.light.toLight = QVector3D(0.3f, 0.9f, 0.1f).normalized();
        frame.light.strength = 1.0f;
        return frame;
    }

private slots:
    void initTestCase()
    {
        QRhiNullInitParams params;
        m_rhi.reset(QRhi::create(QRhi::Null, &params));
        QVERIFY(m_rhi);
    }

    void mapFollowsTheQuality()
    {
        QcShadowMap shadows;
        QVERIFY(!shadows.holdsResources());
        QVERIFY(shadows.prepare(m_rhi.get(), shadowsOf(Quality::Low)));
        QVERIFY(shadows.ready());
        QCOMPARE(shadows.cascadeCount(), 1);
        QCOMPARE(shadows.resolution(), shadowsOf(Quality::Low).resolution);
        QCOMPARE(shadows.target()->pixelSize(), QSize(shadows.resolution(), shadows.resolution()));

        QVERIFY(shadows.replaces(m_rhi.get(), shadowsOf(Quality::High)));
        QVERIFY(shadows.prepare(m_rhi.get(), shadowsOf(Quality::High)));
        QCOMPARE(shadows.cascadeCount(), QcRenderOptions::maxShadowCascades);
        QCOMPARE(shadows.target()->pixelSize(), QSize(3 * shadows.resolution(), shadows.resolution()));

        // A wider filter or another distance keeps the map.
        QcRenderOptions::Shadows softer = shadowsOf(Quality::High);
        softer.filterTaps = 1;
        softer.distance = 64.0f;
        QVERIFY(!shadows.replaces(m_rhi.get(), softer));
        const QRhiRenderTarget *target = shadows.target();
        QVERIFY(shadows.prepare(m_rhi.get(), softer));
        QCOMPARE(shadows.target(), target);

        // The classic mode has no cascades: nothing stays on the GPU.
        QVERIFY(!shadows.prepare(m_rhi.get(), QcRenderOptions{}.shadows()));
        QVERIFY(!shadows.ready());
        QVERIFY(!shadows.holdsResources());
        QVERIFY(!shadows.replaces(m_rhi.get(), shadowsOf(Quality::Medium)));
        QVERIFY(shadows.prepare(m_rhi.get(), shadowsOf(Quality::Medium)));
        QCOMPARE(shadows.cascadeCount(), 2);
    }

    void bindingsAndCastersFollowTheAtlas()
    {
        QRhiCommandBuffer *commands = nullptr;
        QCOMPARE(m_rhi->beginOffscreenFrame(&commands), QRhi::FrameOpSuccess);
        QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();
        QcWorldDeviceResources resources = device(updates);
        QVERIFY(resources.hasAtlas());

        QcShadowMap shadows;
        QVERIFY(!shadows.receiverBindings(resources, {}));
        QVERIFY(!shadows.casters(resources));
        QVERIFY(shadows.prepare(m_rhi.get(), shadowsOf(Quality::Medium)));
        QRhiShaderResourceBindings *receivers = shadows.receiverBindings(resources, {});
        QVERIFY(receivers);
        QCOMPARE(shadows.receiverBindings(resources, {}), receivers);
        // The atlas bindings and two more: another layout than the classic pipelines'.
        QCOMPARE(std::distance(receivers->cbeginBindings(), receivers->cendBindings()), 5);
        QVERIFY(!receivers->isLayoutCompatible(resources.atlasBindings()));
        const std::optional<QcShadowMap::Casters> casters = shadows.casters(resources);
        QVERIFY(casters);
        QVERIFY(casters->solid && casters->cutout && casters->entities && casters->bindings);

        // A new atlas: what referred to the old one goes, and is made again for the new one.
        shadows.releaseAtlasBindings();
        resources.replaceAtlas({}, {}, 2, updates);
        QVERIFY(shadows.receiverBindings(resources, {}));
        QVERIFY(shadows.casters(resources));

        shadows.upload(updates, noon());
        QVERIFY(shadows.casts());
        QCOMPARE(shadows.casterOffset(1).second % static_cast<quint32>(m_rhi->ubufAlignment()), 0u);
        QVERIFY(shadows.casterOffset(1).second >= shadows.casterOffset(0).second + 80);

        shadows.release();
        QVERIFY(!shadows.holdsResources());
        QVERIFY(!shadows.ready());
        QVERIFY(!shadows.casters(resources));
        updates->release();
        QCOMPARE(m_rhi->endOffscreenFrame(), QRhi::FrameOpSuccess);
    }

    // The maps in item frames are drawn with the entity pipeline (decision E-896): built with the
    // atlas bindings (classic, or realistic without a map) it takes the framed maps' own bindings,
    // built with the receivers' bindings it takes framedMapBindings(); each fits only its pipeline.
    void framedMapsFitTheEntityPipeline()
    {
        QRhiCommandBuffer *commands = nullptr;
        QCOMPARE(m_rhi->beginOffscreenFrame(&commands), QRhi::FrameOpSuccess);
        QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();
        QcWorldDeviceResources resources = device(updates);
        QcShadowMap shadows;
        QVERIFY(shadows.prepare(m_rhi.get(), shadowsOf(Quality::Medium)));
        QVERIFY(!shadows.framedMapBindings(resources, {}));
        QImage map(QcMapData::size, QcMapData::size, QImage::Format_RGBA8888);
        map.fill(Qt::darkGreen);
        QList<QcFramedMapAtlas::Tile> tiles(QcFramedMapAtlas::tileCount);
        tiles[0] = {map, 1};
        QVERIFY(resources.framedMaps.upload(m_rhi.get(), updates, tiles, resources.frameUniforms.get()) > 0);

        std::unique_ptr<QRhiTexture> colour(m_rhi->newTexture(QRhiTexture::RGBA8, {64, 64}, 1, QRhiTexture::RenderTarget));
        QVERIFY(colour->create());
        std::unique_ptr<QRhiTextureRenderTarget> target(m_rhi->newTextureRenderTarget({QRhiColorAttachment(colour.get())}));
        std::unique_ptr<QRhiRenderPassDescriptor> pass(target->newCompatibleRenderPassDescriptor());
        target->setRenderPassDescriptor(pass.get());
        QVERIFY(target->create());
        const QLoggingCategory category("test");
        const auto entityPipeline = [&](QRhiShaderResourceBindings *bindings, const QString &variant) {
            return QcRenderPipelines::createEntityPipeline(m_rhi.get(), target.get(), bindings,
                                                           QcRenderPipelines::loadShader(u":/shaders/entity"_s + variant + u".vert.qsb"_s, category),
                                                           QcRenderPipelines::loadShader(u":/shaders/entity"_s + variant + u".frag.qsb"_s, category));
        };

        const std::unique_ptr<QRhiGraphicsPipeline> classic = entityPipeline(resources.atlasBindings(), QString());
        const std::unique_ptr<QRhiGraphicsPipeline> receiving = entityPipeline(shadows.receiverBindings(resources, {}), u"_shadow"_s);
        QVERIFY(classic && receiving);
        QRhiShaderResourceBindings *const framed = resources.framedMaps.bindings();
        QRhiShaderResourceBindings *const framedReceiving = shadows.framedMapBindings(resources, {});
        QVERIFY(framed && framedReceiving);
        QVERIFY(classic->shaderResourceBindings()->isLayoutCompatible(framed));
        QVERIFY(receiving->shaderResourceBindings()->isLayoutCompatible(framedReceiving));
        QVERIFY(!receiving->shaderResourceBindings()->isLayoutCompatible(framed));
        QVERIFY(!classic->shaderResourceBindings()->isLayoutCompatible(framedReceiving));

        // Released with the other bindings that refer to the map.
        shadows.releaseAtlasBindings();
        QVERIFY(shadows.framedMapBindings(resources, {}));
        updates->release();
        QCOMPARE(m_rhi->endOffscreenFrame(), QRhi::FrameOpSuccess);
    }

    // No light, no strength or NaN: the receivers keep the classic light, the step draws nothing.
    void uploadWithoutALightCastsNothing()
    {
        QcShadowMap shadows;
        QVERIFY(shadows.prepare(m_rhi.get(), shadowsOf(Quality::Low)));
        QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();
        QcShadowMap::Frame frame = noon();
        shadows.upload(updates, frame);
        QVERIFY(shadows.casts());
        const QMatrix4x4 matrix = shadows.casterMatrix(0);
        QVERIFY(!matrix.isIdentity());

        frame.light.strength = 0.0f;
        shadows.upload(updates, frame);
        QVERIFY(!shadows.casts());
        frame = noon();
        frame.camera.x = std::numeric_limits<double>::quiet_NaN();
        shadows.upload(updates, frame);
        QVERIFY(!shadows.casts());
        frame = noon();
        frame.light.toLight = QVector3D();
        shadows.upload(updates, frame);
        QVERIFY(!shadows.casts());
        frame = noon();
        frame.light.strength = std::numeric_limits<float>::quiet_NaN();
        shadows.upload(updates, frame);
        QVERIFY(!shadows.casts());
        updates->release();
    }

    // A map larger than the GPU allows is refused once and not tried again until reset().
    void refusedRequestIsRemembered()
    {
        QcRenderOptions::Shadows huge = shadowsOf(Quality::High);
        huge.resolution = m_rhi->resourceLimit(QRhi::TextureSizeMax);
        QcShadowMap shadows;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("larger than the GPU allows")));
        QVERIFY(!shadows.prepare(m_rhi.get(), huge));
        QVERIFY(!shadows.holdsResources());
        QTest::failOnWarning(QRegularExpression(QStringLiteral("Sun shadows")));
        QVERIFY(!shadows.prepare(m_rhi.get(), huge));
        // Another request is tried.
        QVERIFY(shadows.prepare(m_rhi.get(), shadowsOf(Quality::Low)));
        shadows.reset();
        QVERIFY(!shadows.holdsResources());
    }
};

QTEST_GUILESS_MAIN(TestShadowMap)
#include "test_shadow_map.moc"
