#pragma once

#include "app/rendering/QcFramePlan.h"
#include "app/rendering/QcGpuMeshStore.h"
#include "app/rendering/QcMaterialTextures.h"
#include "app/rendering/QcPostProcess.h"
#include "app/rendering/QcRenderSample.h"
#include "app/rendering/QcShadowMap.h"
#include "app/rendering/QcWaterSurface.h"
#include "app/rendering/QcWorldRendererResources.h"
#include "app/rendering/QcWorldView.h"

#include <QQuickRhiItemRenderer>
#include <rhi/qrhi.h>

#include <functional>
#include <optional>
#include <utility>
#include <vector>

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
    // A section in view and the entry of its origin (QcDrawOrigins).
    struct VisibleSection
    {
        const QcGpuMesh *mesh = nullptr;
        quint32 origin = 0;
    };

    // What the steps of one frame share: the sections in view and where the camera is relative to
    // the render anchor (decision E-883).
    struct FrameView
    {
        QList<VisibleSection> visible;
        // The underside of the world faces down from y = 0: from above it is always turned away.
        bool belowWorld = false;
        // The camera's offset from the render anchor (QcDrawOrigins::follow).
        QVector3D camera;
        // Whether a section in view has water: without, the water's copy and steps are left out.
        bool water = false;
    };

    // Both groups and the mesh store for the new rhi(), the pass group first.
    void replaceDevice();
    // Releases the pass group (on its own: a render target its pipelines do not fit).
    void dropPass();
    // The plan of the frame's render mode (decision E-899): the realistic one when its scene target
    // is ready, else the classic one; a plan with another target drops the pass group, and the
    // post-processing is released while the classic plan draws.
    void choosePlan(QSize outputSize, QRhiResourceUpdateBatch *updates);
    // The sun shadows of the realistic mode's quality (decision E-903); the atlas pipelines are
    // released whenever they no longer fit them (another map, receivers with or without shadows).
    void prepareShadows();
    // The material maps for the receivers (decision E-908): the pipelines and bindings that refer to
    // other textures are released first.
    void prepareMaterials(QRhiResourceUpdateBatch *updates);
    // The cascades of this frame for the casters and the receivers.
    void uploadShadows(QRhiResourceUpdateBatch *updates, QVector3D drawCamera);
    // The water of the realistic mode (decision E-906): the copy of the scene and the water's
    // pipeline for this frame (m_waterDraw), else the plain water pipeline draws it.
    void prepareWater();
    void uploadWater(QRhiResourceUpdateBatch *updates, const QMatrix4x4 &viewProjection) const;
    // The render target of a group of steps.
    [[nodiscard]] QRhiRenderTarget *targetOf(QcRenderTarget target) const;
    // The shadow step: every cascade into its tile of the map, in a render pass of its own that
    // takes the frame's `updates`.
    void drawShadowMap(QRhiCommandBuffer *commandBuffer, QRhiResourceUpdateBatch *updates);
    // The sections, then the entities, whose boxes reach into cascade `index`.
    void drawCascade(QRhiCommandBuffer *commandBuffer, const QcShadowMap::Casters &casters, int index);
    // Sets a caster pipeline with the casters' bindings at the cascade's dynamic `offset`.
    void useCaster(QRhiCommandBuffer *commandBuffer, QRhiGraphicsPipeline *pipeline, QRhiShaderResourceBindings *bindings,
                   QRhiCommandBuffer::DynamicOffset offset);
    // What the steps before post-processing draw into: the scene target in the realistic mode,
    // else the item's output.
    [[nodiscard]] QRhiRenderTarget *drawTarget() const;
    // The composite pass's input of this frame.
    [[nodiscard]] QcPostProcess::Composite composite(const QMatrix4x4 &viewProjection) const;
    // Benchmark: CPU time of the render thread since the last frame and the GPU time.
    void startSample(QRhiCommandBuffer *commandBuffer);
    // Atlas, pipelines and the uploads of this frame.
    void prepareResources(QRhiResourceUpdateBatch *updates);
    void uploadFrameUniforms(QRhiResourceUpdateBatch *updates, const QMatrix4x4 &viewProjection, QVector3D drawCamera, QSize outputSize) const;
    // The sections inside the view frustum of `viewProjection` (relative to the camera), with the
    // camera's offset from the render anchor `drawCamera`; uploads the draw origins written since
    // the last frame (QcDrawOrigins) into `updates`. The view is kept for the next frame's lists.
    const FrameView &cull(const QMatrix4x4 &viewProjection, QVector3D drawCamera, QRhiResourceUpdateBatch *updates);
    // The square distance of a section's middle from the camera, for the passes sorted back to front.
    [[nodiscard]] static float distanceSquared(const QcGpuMesh &mesh, const QcDrawOrigins &origins, QVector3D camera);
    // The steps of the plan, one GPU render pass per target.
    void drawPlan(QRhiCommandBuffer *commandBuffer, QRhiResourceUpdateBatch *updates, const FrameView &view, QSize outputSize);
    void drawPass(QRhiCommandBuffer *commandBuffer, QcRenderPass pass, const FrameView &view);
    void drawOpaque(QRhiCommandBuffer *commandBuffer, const FrameView &view);
    void drawEntities(QRhiCommandBuffer *commandBuffer);
    // Back to front by section centre; with the plan's water drawn here too, the water of a section
    // comes right before its other translucent faces.
    void drawTranslucent(QRhiCommandBuffer *commandBuffer, const FrameView &view);
    // The water lists alone, back to front, with the water pipeline (a plan with a Water step).
    void drawWater(QRhiCommandBuffer *commandBuffer, const FrameView &view);
    // The translucent lists the water may cover, back to front, before the water's copy (decision
    // E-906): those of the sections with water, all of them while the eye is under water.
    void drawTranslucentBehindWater(QRhiCommandBuffer *commandBuffer, const FrameView &view);
    void drawOutlineAndHand(QRhiCommandBuffer *commandBuffer);
    // Sets the pipeline with its shader resources (the pipeline's own without `resources`) and
    // forgets the bound vertex input.
    void usePipeline(QRhiCommandBuffer *commandBuffer, QRhiGraphicsPipeline *pipeline, QRhiShaderResourceBindings *resources = nullptr);
    // `count` indices from `firstIndex`, all of them when `count` is 0, placed by the origin entry
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
    [[nodiscard]] const Binding &bound() const { return m_bound; }
    void setBound(const Binding &binding) { m_bound = binding; }
    Binding m_bound;
    // Draws take a base vertex, so lists sharing buffers need no new binding (QRhi::BaseVertex).
    bool m_baseVertex = false;
    // Draws pick their origin entry as their first instance (QRhi::BaseInstance), so the origins
    // are bound once with the buffers; without it they are bound at each draw's entry.
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

    // The plan drawn (choosePlan()).
    [[nodiscard]] const QcFramePlan &plan() const { return m_plan; }
    void setPlan(const QcFramePlan &plan) { m_plan = plan; }
    QcFramePlan m_plan = QcFramePlan::classic();
    // Both plans, made once for each QRhi (replaceDevice()).
    QcFramePlan m_classicPlan = QcFramePlan::classic();
    QcFramePlan m_realisticPlan = QcFramePlan::realistic();
    bool m_warnedRealistic = false;
    // The scene target, bloom and composite of the realistic mode (decision E-899).
    // NOLINT(qc-member-access): an owned part like m_pass; its own methods change it, the renderer only calls them.
    QcPostProcess m_post;
    // The material maps of the realistic mode (decision E-908), declared before the shadow map
    // whose receivers' bindings refer to them.
    // NOLINT(qc-member-access): an owned part like m_post; its own methods change it, the renderer only calls them.
    QcMaterialTextures m_materials;
    // The sun's shadow map of the realistic mode (decision E-903).
    // NOLINT(qc-member-access): an owned part like m_post; its own methods change it, the renderer only calls them.
    QcShadowMap m_shadows;
    // The copy of the scene and the water's pipeline of the realistic mode (decision E-906), and
    // what this frame draws the water with (none: m_pass.waterPipeline).
    // NOLINT(qc-member-access): an owned part like m_post; its own methods change it, the renderer only calls them.
    QcWaterSurface m_water;
    std::optional<QcWaterSurface::Draw> m_waterDraw;

    // The sections on the GPU and the meshes queued for them, with the draw origins.
    QcGpuMeshStore m_meshStore;
    // The renderer's entries in the draw origins, added with each new store (replaceDevice()).
    quint32 m_entityOrigin = 0;
    quint32 m_framedMapOrigin = 0;
    quint32 m_handOrigin = 0;
    // The frame's view and the culling's list by page, kept so that their lists keep their capacity.
    FrameView m_view;
    std::vector<std::pair<quint32, const QcGpuMesh *>> m_paged;
    // The sections of one cascade of the shadow step, by page.
    // NOLINT(qc-member-access): a scratch list like m_paged, kept only for its capacity.
    std::vector<std::pair<quint32, const QcGpuMesh *>> m_casters;
    QList<std::pair<float, VisibleSection>> m_sorted;
    // A new QRhi replaced a previous one and with it the sections on the GPU; reported to the view
    // in the next synchronize().
    bool m_meshesLost = false;

    // Benchmark samples, handed to the view in synchronize() while it collects them
    // (m_frame.collectRenderSamples).
    QList<QcRenderSample> m_samples;
    QcRenderSample m_sample;
    qint64 m_lastThreadCpuNs = -1;
};
