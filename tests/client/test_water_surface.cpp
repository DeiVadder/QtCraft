#include "app/rendering/QcPostProcess.h"
#include "app/rendering/QcShadowMap.h"
#include "app/rendering/QcWaterSurface.h"
#include "app/rendering/QcWorldRendererResources.h"
#include "clientcore/settings/QcRenderOptions.h"
#include "common/core/QcDowncast.h"

#include <QImage>
#include <QTest>
#include <rhi/qrhi.h>

#include <memory>

// The water of the realistic mode (decision E-906) on QRhi's Null backend: the scene's target that
// keeps its contents fits the scene's pipelines, the copy follows the scene's size, the water's
// pipeline is made with the atlas bindings plus the copy and the uniforms (and the shadow receivers'
// entries when there are shadows), its bindings have its layout, and the classic mode, release()
// and a new atlas leave nothing behind that refers to the old resources. The shaders are baked into
// the test (tests/client/tests.cmake).
class TestWaterSurface : public QObject
{
    Q_OBJECT

    struct Output
    {
        std::unique_ptr<QRhiTexture> colour;
        std::unique_ptr<QRhiRenderPassDescriptor> pass;
        std::unique_ptr<QRhiTextureRenderTarget> target;
    };

    std::unique_ptr<QRhi> m_rhi;

    std::unique_ptr<Output> makeOutput(QSize size) const
    {
        auto output = std::make_unique<Output>();
        output->colour.reset(m_rhi->newTexture(QRhiTexture::RGBA8, size, 1, QRhiTexture::RenderTarget));
        if (!output->colour->create()) {
            return nullptr;
        }
        output->target.reset(m_rhi->newTextureRenderTarget({QRhiColorAttachment(output->colour.get())}));
        output->pass.reset(output->target->newCompatibleRenderPassDescriptor());
        output->target->setRenderPassDescriptor(output->pass.get());
        return output->target->create() ? std::move(output) : nullptr;
    }

    static QcRenderOptions realistic(QcRenderOptions::Quality quality = QcRenderOptions::Quality::Medium)
    {
        return {.mode = QcRenderOptions::Mode::Realistic, .quality = quality};
    }

    bool preparePost(QcPostProcess &post, QSize size, QRhiRenderTarget *output) const
    {
        QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();
        const bool ready = post.prepare(m_rhi.get(), size, realistic(), output, updates);
        updates->release();
        return ready;
    }

    QcWorldDeviceResources device(quint64 version = 1) const
    {
        QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();
        QcWorldDeviceResources result = QcWorldDeviceResources::create(m_rhi.get());
        QImage atlas(10, 10, QImage::Format_RGBA8888);
        atlas.fill(Qt::gray);
        QImage animations(1, 1, QImage::Format_RGBA8888);
        animations.fill(Qt::transparent);
        result.replaceAtlas({atlas}, animations, version, updates);
        updates->release();
        return result;
    }

private slots:
    void initTestCase()
    {
        QRhiNullInitParams params;
        m_rhi.reset(QRhi::create(QRhi::Null, &params));
        QVERIFY(m_rhi);
    }

    // The steps after the copy draw into the scene's textures again, with a pass that keeps them.
    void theSceneContinuesOnTheSameTextures()
    {
        const std::unique_ptr<Output> output = makeOutput({64, 32});
        QVERIFY(output);
        QcPostProcess post;
        QCOMPARE(post.sceneGeneration(), quint64{0});
        QVERIFY(preparePost(post, {64, 32}, output->target.get()));
        QRhiRenderTarget *continued = post.sceneContinuedTarget();
        QVERIFY(continued);
        // Its pass differs only in loading instead of clearing, which the Vulkan specification counts
        // as compatible; QRhi's own isCompatible() may still say no on Vulkan, so it is not asked.
        const auto *textureTarget = qcDowncast<QRhiTextureRenderTarget>(continued);
        QVERIFY(textureTarget->flags().testFlag(QRhiTextureRenderTarget::PreserveColorContents));
        QVERIFY(textureTarget->flags().testFlag(QRhiTextureRenderTarget::PreserveDepthStencilContents));
        QCOMPARE(textureTarget->description().colorAttachmentAt(0)->texture(), post.sceneColour());
        QCOMPARE(textureTarget->description().depthTexture(), post.sceneDepth());

        const quint64 first = post.sceneGeneration();
        QVERIFY(first != 0);
        QVERIFY(preparePost(post, {64, 32}, output->target.get()));
        QCOMPARE(post.sceneGeneration(), first);
        QVERIFY(preparePost(post, {32, 16}, output->target.get()));
        QVERIFY(post.sceneGeneration() != first);
        QCOMPARE(post.sceneContinuedTarget()->pixelSize(), QSize(32, 16));
        post.release();
        QVERIFY(!post.sceneContinuedTarget());
        QCOMPARE(post.sceneGeneration(), quint64{0});
    }

    // The copy has the scene's size and is made again with a new scene; the classic mode and
    // release() hold nothing.
    void theCopyFollowsTheScene()
    {
        const std::unique_ptr<Output> output = makeOutput({64, 32});
        QVERIFY(output);
        QcPostProcess post;
        QcWaterSurface water;
        QVERIFY(!water.prepare(m_rhi.get(), post, realistic().water()));
        QVERIFY(!water.holdsResources());
        QVERIFY(preparePost(post, {64, 32}, output->target.get()));

        QVERIFY(!water.prepare(m_rhi.get(), post, QcRenderOptions{}.water()));
        QVERIFY(!water.holdsResources());
        QVERIFY(water.prepare(m_rhi.get(), post, realistic(QcRenderOptions::Quality::Low).water()));
        QVERIFY(water.ready());
        QCOMPARE(water.behind()->pixelSize(), QSize(64, 32));
        QCOMPARE(water.behind()->format(), QRhiTexture::RGBA16F);
        const QRhiTexture *first = water.behind();
        // The same scene keeps the copy, another quality too.
        QVERIFY(water.prepare(m_rhi.get(), post, realistic(QcRenderOptions::Quality::High).water()));
        QCOMPARE(water.behind(), first);

        QVERIFY(preparePost(post, {32, 16}, output->target.get()));
        QVERIFY(water.prepare(m_rhi.get(), post, realistic().water()));
        QCOMPARE(water.behind()->pixelSize(), QSize(32, 16));

        water.release();
        QVERIFY(!water.holdsResources());
        QVERIFY(!water.ready());
        QVERIFY(water.prepare(m_rhi.get(), post, realistic().water()));
        water.reset();
        QVERIFY(!water.holdsResources());
    }

    // The water's bindings: the atlas's three, the receivers' two with shadows, the copy and the
    // uniforms; the pipeline is made with them (E-896), again when the shadows come or go and after
    // releaseAtlas().
    void thePipelineHasTheWatersLayout()
    {
        const std::unique_ptr<Output> output = makeOutput({64, 32});
        QVERIFY(output);
        QcPostProcess post;
        QVERIFY(preparePost(post, {64, 32}, output->target.get()));
        QcWaterSurface water;
        QVERIFY(water.prepare(m_rhi.get(), post, realistic().water()));
        QcWorldDeviceResources resources = device();
        QVERIFY(!water.pipeline(QcWorldDeviceResources{}, nullptr, post.sceneContinuedTarget()));

        const std::optional<QcWaterSurface::Draw> plain = water.pipeline(resources, nullptr, post.sceneContinuedTarget());
        QVERIFY(plain);
        QCOMPARE(plain->bindings->bindingCount(), 5);
        QVERIFY(plain->pipeline->shaderResourceBindings()->isLayoutCompatible(plain->bindings));
        QVERIFY(!plain->bindings->isLayoutCompatible(resources.atlasBindings()));
        // Asked again in the same state: the same pipeline.
        const std::optional<QcWaterSurface::Draw> again = water.pipeline(resources, nullptr, post.sceneContinuedTarget());
        QVERIFY(again);
        QCOMPARE(again->pipeline, plain->pipeline);

        QcShadowMap shadows;
        QVERIFY(shadows.prepare(m_rhi.get(), realistic().shadows()));
        const QList<QRhiShaderResourceBinding> entries = shadows.receiverEntries();
        QCOMPARE(entries.size(), 2);
        const std::optional<QcWaterSurface::Draw> shadowed = water.pipeline(resources, &shadows, post.sceneContinuedTarget());
        QVERIFY(shadowed);
        QCOMPARE(shadowed->bindings->bindingCount(), 7);
        QVERIFY(shadowed->pipeline->shaderResourceBindings()->isLayoutCompatible(shadowed->bindings));
        // The receivers' layout plus the water's two: not the plain water's, nor the receivers'.
        QRhiShaderResourceBindings *receivers = shadows.receiverBindings(resources, {});
        QVERIFY(receivers);
        QVERIFY(!shadowed->bindings->isLayoutCompatible(receivers));
        for (qsizetype i = 0; i < receivers->bindingCount(); ++i) {
            QVERIFY(*shadowed->bindings->bindingAt(i) == *receivers->bindingAt(i));
        }

        // A new atlas: what refers to the old one goes first, and is made again for the new one.
        water.releaseAtlas();
        shadows.releaseAtlasBindings();
        resources = device(2);
        const std::optional<QcWaterSurface::Draw> renewed = water.pipeline(resources, &shadows, post.sceneContinuedTarget());
        QVERIFY(renewed);
        QVERIFY(renewed->pipeline->shaderResourceBindings()->isLayoutCompatible(renewed->bindings));

        shadows.release();
        QVERIFY(shadows.receiverEntries().isEmpty());
        QCOMPARE(shadows.generation(), quint64{0});
        water.release();
        QVERIFY(!water.pipeline(resources, nullptr, post.sceneContinuedTarget()));
    }

    // Another shadow map (another cascade count or resolution) makes the water's bindings anew:
    // they never refer to a released map.
    void anotherShadowMapMakesNewBindings()
    {
        const std::unique_ptr<Output> output = makeOutput({64, 32});
        QVERIFY(output);
        QcPostProcess post;
        QVERIFY(preparePost(post, {64, 32}, output->target.get()));
        QcWaterSurface water;
        QVERIFY(water.prepare(m_rhi.get(), post, realistic().water()));
        const QcWorldDeviceResources resources = device();
        QcShadowMap shadows;
        QVERIFY(shadows.prepare(m_rhi.get(), realistic(QcRenderOptions::Quality::Medium).shadows()));
        const quint64 first = shadows.generation();
        QVERIFY(first != 0);
        QVERIFY(water.pipeline(resources, &shadows, post.sceneContinuedTarget()));

        const QcRenderOptions::Shadows high = realistic(QcRenderOptions::Quality::High).shadows();
        QVERIFY(shadows.replaces(m_rhi.get(), high));
        QVERIFY(shadows.prepare(m_rhi.get(), high));
        QVERIFY(shadows.generation() != first);
        const std::optional<QcWaterSurface::Draw> renewed = water.pipeline(resources, &shadows, post.sceneContinuedTarget());
        QVERIFY(renewed);
        const QList<QRhiShaderResourceBinding> entries = shadows.receiverEntries();
        QCOMPARE(entries.size(), 2);
        QVERIFY(*renewed->bindings->bindingAt(3) == entries[0]);
        QVERIFY(*renewed->bindings->bindingAt(4) == entries[1]);
        // A filter or distance of the same map keeps the bindings.
        QcRenderOptions::Shadows wider = high;
        wider.filterTaps = 1;
        QVERIFY(shadows.prepare(m_rhi.get(), wider));
        const std::optional<QcWaterSurface::Draw> kept = water.pipeline(resources, &shadows, post.sceneContinuedTarget());
        QVERIFY(kept);
        QCOMPARE(kept->bindings, renewed->bindings);
    }
};

QTEST_GUILESS_MAIN(TestWaterSurface)
#include "test_water_surface.moc"
