// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/realistic/QcPostProcess.h"
#include "clientcore/settings/QcRenderOptions.h"

#include <QTest>
#include <rhi/qrhi.h>

#include <memory>

// The GPU resources of the realistic mode (decision E-899) on QRhi's Null backend: the scene target
// follows the output's size, the bloom follows the quality and halves only as far as the size
// allows, the ambient occlusion (decision E-902) follows the quality and the size, release()
// leaves nothing on the GPU, and a refused request is not tried again every frame.
// The post-processing shaders are baked into the test (tests/client/tests.cmake).
class TestPostProcess : public QObject
{
    Q_OBJECT

    // An output like the item's own: colour with depth-stencil.
    struct Output
    {
        std::unique_ptr<QRhiTexture> colour;
        std::unique_ptr<QRhiRenderBuffer> depth;
        std::unique_ptr<QRhiRenderPassDescriptor> pass;
        std::unique_ptr<QRhiTextureRenderTarget> target;
    };

    using Quality = QcRenderOptions::Quality;

    std::unique_ptr<QRhi> m_rhi;

    std::unique_ptr<Output> makeOutput(QSize size) const
    {
        auto output = std::make_unique<Output>();
        output->colour.reset(m_rhi->newTexture(QRhiTexture::RGBA8, size, 1, QRhiTexture::RenderTarget));
        output->depth.reset(m_rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, size));
        if (!output->colour->create() || !output->depth->create()) {
            return nullptr;
        }
        QRhiTextureRenderTargetDescription description{QRhiColorAttachment(output->colour.get())};
        description.setDepthStencilBuffer(output->depth.get());
        output->target.reset(m_rhi->newTextureRenderTarget(description));
        output->pass.reset(output->target->newCompatibleRenderPassDescriptor());
        output->target->setRenderPassDescriptor(output->pass.get());
        return output->target->create() ? std::move(output) : nullptr;
    }

    // One prepare() with a batch of the frame, released again (nothing is drawn).
    bool prepare(QcPostProcess &post, QSize size, QcRenderOptions::Quality quality, QRhiRenderTarget *output) const
    {
        QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();
        const QcRenderOptions options{.mode = QcRenderOptions::Mode::Realistic, .quality = quality};
        const bool ready = post.prepare(m_rhi.get(), size, options, output, updates);
        updates->release();
        return ready;
    }

private slots:
    void initTestCase()
    {
        QRhiNullInitParams params;
        m_rhi.reset(QRhi::create(QRhi::Null, &params));
        QVERIFY(m_rhi);
    }

    void bloomFollowsTheQualityAndTheSize()
    {
        const std::unique_ptr<Output> output = makeOutput({640, 400});
        QVERIFY(output);
        QcPostProcess post;
        QVERIFY(prepare(post, {640, 400}, Quality::Medium, output->target.get()));
        QVERIFY(post.blooms());
        QCOMPARE(post.sceneTarget()->pixelSize(), QSize(640, 400));
        QRhiRenderPassDescriptor *const scenePass = post.sceneTarget()->renderPassDescriptor();

        QVERIFY(prepare(post, {640, 400}, Quality::High, output->target.get()));
        QVERIFY(post.blooms());
        QVERIFY(prepare(post, {640, 400}, Quality::Low, output->target.get()));
        QVERIFY(!post.blooms());
        QVERIFY(post.sceneTarget());
        QVERIFY(prepare(post, {640, 400}, Quality::Medium, output->target.get()));
        QVERIFY(post.blooms());

        // A new size makes new textures; the pass descriptor stays, so the world's pipelines fit.
        QVERIFY(prepare(post, {320, 200}, Quality::Medium, output->target.get()));
        QCOMPARE(post.sceneTarget()->pixelSize(), QSize(320, 200));
        QCOMPARE(post.sceneTarget()->renderPassDescriptor(), scenePass);
        QVERIFY(post.blooms());
    }

    // Low draws without ambient occlusion, Medium and High with it (the samples differ in the
    // uniforms only); a new size makes a new occlusion texture at half of it.
    void occlusionFollowsTheQualityAndTheSize()
    {
        const std::unique_ptr<Output> output = makeOutput({640, 400});
        QVERIFY(output);
        QcPostProcess post;
        QVERIFY(prepare(post, {640, 400}, Quality::Low, output->target.get()));
        QVERIFY(!post.occludes());
        QVERIFY(prepare(post, {640, 400}, Quality::Medium, output->target.get()));
        QVERIFY(post.occludes());
        QVERIFY(prepare(post, {640, 400}, Quality::High, output->target.get()));
        QVERIFY(post.occludes());
        QVERIFY(prepare(post, {640, 400}, Quality::Low, output->target.get()));
        QVERIFY(!post.occludes());
        QVERIFY(post.sceneTarget());
        QVERIFY(prepare(post, {7, 3}, Quality::Medium, output->target.get()));
        QVERIFY(post.occludes());
        QVERIFY(prepare(post, {1, 1}, Quality::High, output->target.get()));
        QVERIFY(post.occludes());
        post.release();
        QVERIFY(!post.holdsResources());
        QVERIFY(!post.occludes());
    }

    // An output one pixel wide cannot be halved: the realistic mode draws without bloom; four by
    // four pixels give one halving of two by two.
    void tinyOutputsDrawWithoutBloom()
    {
        const std::unique_ptr<Output> output = makeOutput({1, 7});
        QVERIFY(output);
        QcPostProcess post;
        QVERIFY(prepare(post, {1, 7}, Quality::High, output->target.get()));
        QVERIFY(!post.blooms());
        QVERIFY(prepare(post, {7, 1}, Quality::High, output->target.get()));
        QVERIFY(!post.blooms());
        QVERIFY(prepare(post, {4, 4}, Quality::High, output->target.get()));
        QVERIFY(post.blooms());
    }

    // Classic, realistic, classic: the classic mode holds nothing on the GPU.
    void releaseLeavesNoResources()
    {
        const std::unique_ptr<Output> output = makeOutput({64, 64});
        QVERIFY(output);
        QcPostProcess post;
        QVERIFY(!post.holdsResources());
        QVERIFY(prepare(post, {64, 64}, Quality::Medium, output->target.get()));
        QVERIFY(post.holdsResources());
        post.release();
        QVERIFY(!post.holdsResources());
        QVERIFY(!post.sceneTarget());
        QVERIFY(!post.blooms());
        QVERIFY(prepare(post, {64, 64}, Quality::Medium, output->target.get()));
        QVERIFY(post.blooms());
    }

    // A refused request releases everything and is answered without a new try (no warning per
    // frame); another request is tried again.
    void refusedRequestIsRemembered()
    {
        const std::unique_ptr<Output> output = makeOutput({64, 64});
        QVERIFY(output);
        QcPostProcess post;
        QVERIFY(prepare(post, {64, 64}, Quality::Medium, output->target.get()));
        QVERIFY(!prepare(post, {0, 64}, Quality::Medium, output->target.get()));
        QVERIFY(!post.holdsResources());
        QVERIFY(!prepare(post, {0, 64}, Quality::Medium, output->target.get()));
        QVERIFY(!post.holdsResources());
        QVERIFY(prepare(post, {64, 64}, Quality::Medium, output->target.get()));
        post.reset();
        QVERIFY(!post.holdsResources());
    }
};

QTEST_GUILESS_MAIN(TestPostProcess)
#include "test_post_process.moc"
