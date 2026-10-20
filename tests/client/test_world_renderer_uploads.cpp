// The world renderer uploads each section once per frame (decision E-413): a section submitted,
// replaced or removed several times between two frames goes up with its latest mesh only. A second
// update of the same section in one resource update batch would release the buffers the first one
// queued for upload, and Metal would crash in QRhiMetal::enqueueResourceUpdates. Renders the world
// view through a QQuickRenderControl into an offscreen texture (Metal; skipped without a device).
// The game's shaders are not part of this test, so no pipeline exists and nothing is drawn, but the
// uploads of every frame still go to the GPU.

#include "app/rendering/QcGpuMeshStore.h"
#include "app/rendering/QcWorldView.h"
#include "clientcore/meshing/QcChunkMesh.h"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQuickGraphicsDevice>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QTest>
#include <rhi/qrhi.h>

#include <memory>

using namespace Qt::StringLiterals;

namespace {

// A section mesh with one opaque quad at height y (its buffers are what the frame uploads).
std::shared_ptr<const QcChunkMesh> quadMesh(float y)
{
    auto mesh = std::make_shared<QcChunkMesh>();
    for (const auto &[x, z] : {std::pair{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}) {
        QcBlockVertex vertex;
        vertex.x = x;
        vertex.y = y;
        vertex.z = z;
        mesh->opaqueVertices.append(vertex);
    }
    mesh->opaqueIndices = {0, 1, 2, 0, 2, 3};
    return mesh;
}

} // namespace

class TestWorldRendererUploads : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QQuickRenderControl> m_control;
    std::unique_ptr<QQuickWindow> m_window;
    std::unique_ptr<QRhiTexture> m_texture;
    std::unique_ptr<QRhiRenderBuffer> m_depthStencil;
    std::unique_ptr<QRhiTextureRenderTarget> m_target;
    std::unique_ptr<QRhiRenderPassDescriptor> m_renderPass;
    QcWorldView *m_view = nullptr;

    bool createTarget()
    {
        QRhi *rhi = m_control->rhi();
        const QSize size(64, 64);
        m_texture.reset(rhi->newTexture(QRhiTexture::RGBA8, size, 1, QRhiTexture::RenderTarget));
        m_depthStencil.reset(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, size, 1));
        if (!m_texture->create() || !m_depthStencil->create()) {
            return false;
        }

        QRhiTextureRenderTargetDescription description{QRhiColorAttachment(m_texture.get())};
        description.setDepthStencilBuffer(m_depthStencil.get());
        m_target.reset(rhi->newTextureRenderTarget(description));
        m_renderPass.reset(m_target->newCompatibleRenderPassDescriptor());
        m_target->setRenderPassDescriptor(m_renderPass.get());
        if (!m_target->create()) {
            return false;
        }
        m_window->setRenderTarget(QQuickRenderTarget::fromRhiRenderTarget(m_target.get()));
        return true;
    }

    void renderFrame()
    {
        m_control->polishItems();
        m_control->beginFrame();
        m_control->sync();
        m_control->render();
        m_control->endFrame();
    }

private slots:
    void initTestCase()
    {
        // Without the game's shaders every pipeline fails; those messages are expected here.
        QLoggingCategory::setFilterRules(u"qtcraft.renderer=false"_s);
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
        m_control = std::make_unique<QQuickRenderControl>();
        m_window = std::make_unique<QQuickWindow>(m_control.get());
        m_window->resize(64, 64);
        if (!m_control->initialize()) {
            m_window.reset();
            m_control.reset();
            return;
        }
        QVERIFY(createTarget());

        m_view = new QcWorldView(m_window->contentItem());
        m_view->setSize(QSizeF(64, 64));
        // Two frames: the view creates its texture and the renderer its resources.
        renderFrame();
        renderFrame();
    }

    void cleanupTestCase()
    {
        delete m_view;
        m_window.reset();
        m_target.reset();
        m_renderPass.reset();
        m_depthStencil.reset();
        m_texture.reset();
        m_control.reset();
    }

    void init()
    {
        if (!m_control) {
            QSKIP("no Metal device for the offscreen render control");
        }
    }

    // A section is drawn when any of its lists has faces: one with only cut-out faces (plants or
    // leaves alone above a section boundary) too; the underside only from below the world (E-756).
    void sectionsWithOnlyCutOutFacesAreDrawable()
    {
        QcGpuMesh mesh;
        QVERIFY(!mesh.isDrawable(false));
        QVERIFY(!mesh.isDrawable(true));
        mesh.cutout.indexCount = 6;
        QVERIFY(mesh.isDrawable(false));
        mesh.cutout.indexCount = 0;
        mesh.underside.indexCount = 6;
        QVERIFY(!mesh.isDrawable(false));
        QVERIFY(mesh.isDrawable(true));
        for (QcGpuBuffers *buffers : {&mesh.opaque, &mesh.translucent, &mesh.water}) {
            mesh.underside.indexCount = 0;
            buffers->indexCount = 6;
            QVERIFY(mesh.isDrawable(false));
            buffers->indexCount = 0;
        }
    }

    // Uploaded, then replaced in the same frame: only the second mesh goes up.
    void replacedBeforeTheFrame()
    {
        for (int frame = 0; frame < 20; ++frame) {
            const quint64 key = 100 + quint64(frame);
            m_view->submitMesh(key, quadMesh(1.0f));
            m_view->submitMesh(key, quadMesh(2.0f));
            m_view->submitMesh(key, quadMesh(3.0f));
            renderFrame();
        }
    }

    // Uploaded, then removed in the same frame: nothing goes up.
    void removedBeforeTheFrame()
    {
        for (int frame = 0; frame < 20; ++frame) {
            const quint64 key = 200 + quint64(frame);
            m_view->submitMesh(key, quadMesh(1.0f));
            m_view->removeMesh(key);
            m_view->submitMesh(key + 1000, quadMesh(1.0f));
            m_view->submitMesh(key + 1000, quadMesh(2.0f));
            m_view->removeMesh(key + 1000);
            renderFrame();
        }
    }

    // Sections that already had a mesh, replaced several times and removed again between frames.
    void churnOfExistingSections()
    {
        for (quint64 key = 300; key < 340; ++key) {
            m_view->submitMesh(key, quadMesh(0.0f));
        }
        renderFrame();
        for (int frame = 0; frame < 30; ++frame) {
            for (quint64 key = 300; key < 340; ++key) {
                m_view->submitMesh(key, quadMesh(float(frame)));
                if ((key + quint64(frame)) % 3 == 0) {
                    m_view->removeMesh(key);
                }
                m_view->submitMesh(key, quadMesh(float(frame) + 0.5f));
            }
            renderFrame();
        }
    }
};

QTEST_MAIN(TestWorldRendererUploads)
#include "test_world_renderer_uploads.moc"
