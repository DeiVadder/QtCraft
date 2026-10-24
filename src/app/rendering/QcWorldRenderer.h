#pragma once

#include "app/rendering/QcFramePlan.h"
#include "app/rendering/QcGpuMeshStore.h"
#include "app/rendering/QcRenderSample.h"
#include "app/rendering/QcWorldRendererResources.h"
#include "app/rendering/QcWorldView.h"

#include <QQuickRhiItemRenderer>
#include <rhi/qrhi.h>

#include <functional>

// Render-thread side of QcWorldView: the drawing of a frame, which is the list of steps of the
// render mode (QcFramePlan, decision E-390), drawn in order into their targets. The GPU resources
// are in two groups by lifetime (decision E-710): QcWorldDeviceResources for the QRhi,
// QcWorldPassResources for the render pass; the sections' buffers are in QcGpuMeshStore.
//
// Thread contract (Qt Quick): every method runs on the render thread. synchronize() runs while the
// GUI thread is blocked and is the only place that touches the QcWorldView: it copies the view's
// FrameInput snapshot into m_frame and takes the queued mesh updates; initialize() and render()
// read only m_frame and the renderer's own state, while the GUI thread may already change the view.
class QcWorldRenderer : public QQuickRhiItemRenderer
{
public:
    QcWorldRenderer() = default;

protected:
    void initialize(QRhiCommandBuffer *commandBuffer) override;
    void synchronize(QQuickRhiItem *item) override;
    void render(QRhiCommandBuffer *commandBuffer) override;

private:
    // A section in view and the entry of its offset from the camera (QcDrawOrigins).
    struct VisibleSection
    {
        const QcGpuMesh *mesh = nullptr;
        quint32 origin = 0;
    };

    // What the steps of one frame share: the sections in view, where the camera is and the entries
    // of the draws' offsets from it (decision E-883).
    struct FrameView
    {
        QList<VisibleSection> visible;
        // The underside of the world faces down from y = 0: from above it is always turned away.
        bool belowWorld = false;
        quint32 entityOrigin = 0;
        quint32 framedMapOrigin = 0;
        quint32 handOrigin = 0;
    };

    // Both groups and the mesh store for the new rhi(), the pass group first.
    void replaceDevice();
    // Releases the pass group (on its own: a render target its pipelines do not fit).
    void dropPass();
    // Benchmark: CPU time of the render thread since the last frame and the GPU time.
    void startSample(QRhiCommandBuffer *commandBuffer);
    // Atlas, pipelines and the uploads of this frame.
    void prepareResources(QRhiResourceUpdateBatch *updates);
    void uploadFrameUniforms(QRhiResourceUpdateBatch *updates, const QMatrix4x4 &viewProjection, QSize outputSize) const;
    // The sections inside the view frustum of `viewProjection` (relative to the camera), with the
    // offsets of the frame's draws from the camera (QcDrawOrigins, uploaded into `updates`).
    FrameView cull(const QMatrix4x4 &viewProjection, QRhiResourceUpdateBatch *updates);
    // The steps of the plan, one GPU render pass per target.
    void drawPlan(QRhiCommandBuffer *commandBuffer, QRhiResourceUpdateBatch *updates, const FrameView &view, QSize outputSize);
    void drawPass(QRhiCommandBuffer *commandBuffer, QcRenderPass pass, const FrameView &view);
    void drawOpaque(QRhiCommandBuffer *commandBuffer, const FrameView &view);
    void drawEntities(QRhiCommandBuffer *commandBuffer, const FrameView &view);
    // Back to front by section centre; with the plan's water drawn here too, the water of a section
    // comes right before its other translucent faces.
    void drawTranslucent(QRhiCommandBuffer *commandBuffer, const FrameView &view);
    // The water lists alone, back to front, with the water pipeline (a plan with a Water step).
    void drawWater(QRhiCommandBuffer *commandBuffer, const FrameView &view);
    void drawOutlineAndHand(QRhiCommandBuffer *commandBuffer, const FrameView &view);
    // Sets the pipeline with its shader resources (the pipeline's own without `resources`) and
    // forgets the bound vertex input.
    void usePipeline(QRhiCommandBuffer *commandBuffer, QRhiGraphicsPipeline *pipeline, QRhiShaderResourceBindings *resources = nullptr);
    // `count` indices from `firstIndex`, all of them when `count` is 0, placed by the offset entry
    // `origin` (QcDrawOrigins); binds the list's buffers only when they differ from the bound ones
    // (with QRhi::BaseVertex, and QRhi::BaseInstance for the offsets).
    void drawBuffers(QRhiCommandBuffer *commandBuffer, const QcGpuList &list, quint32 origin, quint32 firstIndex = 0, quint32 count = 0);
    // The pipeline the translucent step draws the water lists with: the translucent one as long as
    // the water pipeline does the same (m_waterLikeTranslucent), so alternating draws need no switch.
    QRhiGraphicsPipeline *waterPipelineInTranslucent() const;
    // The water and translucent faces of one section in the mesher's order, switching pipelines.
    void drawTranslucentRuns(VisibleSection section, const std::function<void(QRhiGraphicsPipeline *)> &use,
                             QRhiCommandBuffer *commandBuffer);

    // The vertex and index buffers bound since the last usePipeline() (decision E-820), and the
    // offset entry the origins are bound at (always 0 with base instances).
    struct Binding
    {
        const QRhiBuffer *vertices = nullptr;
        const QRhiBuffer *indices = nullptr;
        QRhiCommandBuffer::IndexFormat indexFormat = QRhiCommandBuffer::IndexUInt16;
        quint32 origin = 0;
        bool operator==(const Binding &) const = default;
    };
    Binding m_bound;
    // Draws take a base vertex, so lists sharing buffers need no new binding (QRhi::BaseVertex).
    bool m_baseVertex = false;
    // Draws pick their offset from the camera as their first instance (QRhi::BaseInstance), so the
    // offsets are bound once with the buffers; without it they are bound at each draw's entry.
    bool m_baseInstance = false;

    // GPU resources by lifetime: the pass group is dropped with the device group and on its own
    // when the render target's pass changes (initialize()).
    QcWorldDeviceResources m_device;
    QcWorldPassResources m_pass;
    // m_pass.waterPipeline has the shaders, blending and resources of m_pass.translucentPipeline. A water
    // pipeline of its own (stage 45.3) sets this to false where it is created.
    bool m_waterLikeTranslucent = true;
    // The frame's input, copied from the view in synchronize().
    QcWorldView::FrameInput m_frame;

    QcFramePlan m_plan = QcFramePlan::classic();
    bool m_warnedTarget = false;

    // The sections on the GPU and the meshes queued for them.
    QcGpuMeshStore m_meshStore;
    // A new QRhi replaced a previous one and with it the sections on the GPU; reported to the view
    // in the next synchronize().
    bool m_meshesLost = false;

    // Benchmark samples, handed to the view in synchronize() while it collects them
    // (m_frame.collectRenderSamples).
    QList<QcRenderSample> m_samples;
    QcRenderSample m_sample;
    qint64 m_lastThreadCpuNs = -1;
};
