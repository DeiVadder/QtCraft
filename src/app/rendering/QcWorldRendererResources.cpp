// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/rendering/QcWorldRendererResources.h"

#include "app/debug/QcAppLogging.h"
#include "app/rendering/QcAtlasTexture.h"
#include "app/rendering/QcDrawOrigins.h"
#include "app/rendering/QcRenderPipelines.h"
#include "clientcore/textures/QcAtlasLayout.h"

#include <array>
#include <cstddef>
#include <utility>

using namespace Qt::StringLiterals;
using QcRenderPipelines::FrameUniforms;
using QcRenderPipelines::loadShader;

namespace {

constexpr int maxOutlineBoxes = QcWorldDeviceResources::maxOutlineBoxes;

// Line vertices of the outline: 12 edges as line segments of two corners per box, for at most
// maxOutlineBoxes boxes.
class OutlineVertices
{
public:
    static constexpr std::size_t floatsPerBox = std::size_t{12} * 2 * 3;

    // Adds the edges of `box` relative to `eye`; false (and nothing added) when maxOutlineBoxes
    // boxes are in.
    [[nodiscard]] bool addBox(const QcAabb &box, const QcVec3 &eye);

    const float *data() const { return m_values.data(); }
    // Number of floats written so far.
    std::size_t size() const { return m_size; }

private:
    std::array<float, static_cast<std::size_t>(maxOutlineBoxes) * floatsPerBox> m_values{};
    std::size_t m_size = 0;
};

bool OutlineVertices::addBox(const QcAabb &box, const QcVec3 &eye)
{
    if (m_size + floatsPerBox > m_values.size()) {
        return false;
    }

    // Slightly larger than the boxes so the lines are not hidden by their faces.
    constexpr float grow = 0.004f;
    constexpr std::array<std::pair<int, int>, 12> edges = {
        {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};

    // Relative to the eye in double, then small floats (decision E-883).
    const QcVec3 low = box.min - eye;
    const QcVec3 high = box.max - eye;
    const float x0 = static_cast<float>(low.x) - grow;
    const float y0 = static_cast<float>(low.y) - grow;
    const float z0 = static_cast<float>(low.z) - grow;
    const float x1 = static_cast<float>(high.x) + grow;
    const float y1 = static_cast<float>(high.y) + grow;
    const float z1 = static_cast<float>(high.z) + grow;
    const std::array<std::array<float, 3>, 8> corners = {{{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1},
                                                          {x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}}};

    for (const auto &[from, to] : edges) {
        for (const int corner : {from, to}) {
            for (const float value : corners[static_cast<std::size_t>(corner)]) {
                m_values[m_size++] = value;
            }
        }
    }
    return true;
}

// What the block pipelines share: device, target, bindings, vertex shader and the layout of
// QcBlockVertex; they differ in the fragment shader and in blending.
struct BlockPipelines
{
    QRhi *rhi = nullptr;
    QRhiRenderTarget *target = nullptr;
    QRhiShaderResourceBindings *bindings = nullptr;
    QShader vertexShader;
    QRhiVertexInputLayout inputLayout;

    // Null (logged) when the GPU refuses the pipeline.
    [[nodiscard]] std::unique_ptr<QRhiGraphicsPipeline> build(const QShader &fragmentShader, bool translucent) const;
};

std::unique_ptr<QRhiGraphicsPipeline> BlockPipelines::build(const QShader &fragmentShader, bool translucent) const
{
    std::unique_ptr<QRhiGraphicsPipeline> pipeline(rhi->newGraphicsPipeline());
    pipeline->setShaderStages({{QRhiShaderStage::Vertex, vertexShader}, {QRhiShaderStage::Fragment, fragmentShader}});
    pipeline->setVertexInputLayout(inputLayout);
    pipeline->setShaderResourceBindings(bindings);
    pipeline->setRenderPassDescriptor(target->renderPassDescriptor());
    pipeline->setSampleCount(target->sampleCount());
    pipeline->setDepthTest(true);
    pipeline->setDepthWrite(!translucent);
    pipeline->setFrontFace(QRhiGraphicsPipeline::CCW);
    pipeline->setCullMode(translucent ? QRhiGraphicsPipeline::None : QRhiGraphicsPipeline::Back);

    if (translucent) {
        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true;
        blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        pipeline->setTargetBlends({blend});
    }

    if (!pipeline->create()) {
        qCCritical(lcRenderer) << "Cannot create graphics pipeline";
        return std::unique_ptr<QRhiGraphicsPipeline>();
    }
    return pipeline;
}

// Pixels per cell of the uploaded atlas texture of `grid`; 0 (no filtering) when the texture is not
// made of whole cells of at least the last level's 10 pixels (the GPU refused every level and took
// the placeholder).
float atlasCellPixelsOf(QSize texture, QSize grid)
{
    const int cell = texture.width() / grid.width();
    const bool whole = texture.width() % grid.width() == 0 && texture.height() == grid.height() * cell;
    return whole && cell >= QcAtlasLayout::cellSize(QcAtlasLayout::minimumMipTile) ? static_cast<float>(cell) : 0.0f;
}

} // namespace

QcWorldDeviceResources QcWorldDeviceResources::create(QRhi *rhi)
{
    QcWorldDeviceResources device;
    device.rhi = rhi;
    device.frameUniforms.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(FrameUniforms)));
    device.frameUniforms->create();
    device.atlasSampler = QcAtlasTexture::createSampler(rhi);
    device.animationSampler = QcAtlasTexture::createAnimationMapSampler(rhi);

    device.outlineBindings.reset(rhi->newShaderResourceBindings());
    device.outlineBindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(
        0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, device.frameUniforms.get())});
    device.outlineBindings->create();
    // 12 edges as line segments per box.
    device.outlineVertices.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
                                                static_cast<quint32>(static_cast<std::size_t>(maxOutlineBoxes)
                                                                     * OutlineVertices::floatsPerBox * sizeof(float))));
    device.outlineVertices->create();
    return device;
}

void QcWorldDeviceResources::replaceAtlas(const QList<QImage> &levels, const QImage &animationMap, quint64 version,
                                          QRhiResourceUpdateBatch *updates)
{
    atlasTried = true;
    // The placeholder is drawn plainly: the block shader's filtering needs a real atlas.
    const bool atlas = !levels.isEmpty() && !levels.front().isNull();
    QList<QImage> uploaded = levels;
    if (!atlas) {
        QImage placeholder(16, 16, QImage::Format_RGBA8888);
        placeholder.fill(Qt::magenta);
        uploaded = {placeholder};
    }

    // A new texture for every atlas (a pack switch changes its size and levels); the bindings that
    // refer to the old one go first.
    bindings.reset();
    atlasTexture = QcAtlasTexture::create(rhi, uploaded, updates);
    animationTexture = QcAtlasTexture::createAnimationMap(rhi, atlas ? animationMap : QImage(), updates);
    uploadedAtlasVersion = version;
    atlasGrid = {1, 1};
    atlasCellPixels = 0.0f;
    if (atlas && atlasTexture) {
        atlasGrid = QcAtlasLayout::gridOf(levels.back().size());
        atlasCellPixels = atlasCellPixelsOf(atlasTexture->pixelSize(), atlasGrid);
    }
}

QRhiShaderResourceBindings *QcWorldDeviceResources::atlasBindings()
{
    if (bindings) {
        return bindings.get();
    }

    bindings.reset(rhi->newShaderResourceBindings());
    bindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, frameUniforms.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, atlasTexture.get(),
                                                  atlasSampler.get()),
        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::VertexStage, animationTexture.get(), animationSampler.get()),
    });
    bindings->create();
    return bindings.get();
}

void QcWorldDeviceResources::uploadOutline(QRhiResourceUpdateBatch *updates, const QList<QcAabb> &selection, const QcVec3 &eye)
{
    if (selection.isEmpty() || (selection == uploadedSelection && eye == uploadedEye) || !outlineVertices) {
        return;
    }

    // The outline shows the first maxOutlineBoxes boxes of a shape.
    OutlineVertices vertices;
    for (const QcAabb &box : selection) {
        if (!vertices.addBox(box, eye)) {
            break;
        }
    }

    updates->updateDynamicBuffer(outlineVertices.get(), 0, static_cast<quint32>(vertices.size() * sizeof(float)), vertices.data());
    outlineVertexCount = static_cast<quint32>(vertices.size() / 3);
    uploadedSelection = selection;
    uploadedEye = eye;
}

bool QcWorldPassResources::fits(const QRhiRenderTarget *target) const
{
    return renderPass && sampleCount == target->sampleCount() && renderPass->isCompatible(target->renderPassDescriptor());
}

void QcWorldPassResources::createAtlasPipelines(QRhi *rhi, QRhiRenderTarget *target, QRhiShaderResourceBindings *atlasBindings,
                                                bool receivesShadows)
{
    atlasPipelinesTried = true;
    shadowReceivers = receivesShadows;
    // The receivers of the sun shadows (decision E-903) are the same shaders built with QC_SHADOWS.
    const QString variant = receivesShadows ? u"_shadow"_s : QString();
    const auto shader = [&variant](const QString &name) { return loadShader(u":/shaders/"_s + name + variant + u".frag.qsb"_s, lcRenderer()); };
    const auto vertexShader = [&variant](const QString &name) {
        return loadShader(u":/shaders/"_s + name + variant + u".vert.qsb"_s, lcRenderer());
    };
    const BlockPipelines blocks{rhi, target, atlasBindings, vertexShader(u"block"_s), QcRenderPipelines::blockLayout()};
    const QShader fragmentShader = shader(u"block"_s);
    // The opaque pass writes alpha 1 (decision E-332): mip levels give cut-out texels an alpha
    // between 0 and 1, which a screenshot or a transparent target would read as half transparent. A
    // colour write mask without alpha did the same but cost up to a third more GPU time. Its solid
    // faces take a shader without `discard`, so the GPU shades only the fragments that stay visible
    // (decision E-756); the cut-out faces keep the alpha test.
    solidPipeline = blocks.build(shader(u"block_solid"_s), false);
    cutoutPipeline = blocks.build(shader(u"block_opaque"_s), false);
    translucentPipeline = blocks.build(fragmentShader, true);
    waterPipeline = blocks.build(fragmentShader, true);

    const QShader entityFragment = shader(u"entity"_s);
    entityPipeline = QcRenderPipelines::createEntityPipeline(rhi, target, atlasBindings, vertexShader(u"entity"_s), entityFragment);
    handPipeline = QcRenderPipelines::createEntityPipeline(rhi, target, atlasBindings, vertexShader(u"hand"_s), entityFragment);
    renderPass.reset(target->renderPassDescriptor()->newCompatibleRenderPassDescriptor());
    sampleCount = target->sampleCount();
}

void QcWorldPassResources::createOutlinePipeline(QRhi *rhi, QRhiRenderTarget *target, QRhiShaderResourceBindings *outlineBindings)
{
    outlinePipelineTried = true;
    std::unique_ptr<QRhiGraphicsPipeline> pipeline(rhi->newGraphicsPipeline());
    pipeline->setShaderStages({{QRhiShaderStage::Vertex, loadShader(u":/shaders/outline.vert.qsb"_s, lcRenderer())},
                               {QRhiShaderStage::Fragment, loadShader(u":/shaders/outline.frag.qsb"_s, lcRenderer())}});

    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({{3 * sizeof(float)}});
    inputLayout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0}});
    pipeline->setVertexInputLayout(inputLayout);
    pipeline->setTopology(QRhiGraphicsPipeline::Lines);
    pipeline->setShaderResourceBindings(outlineBindings);
    pipeline->setRenderPassDescriptor(target->renderPassDescriptor());
    pipeline->setSampleCount(target->sampleCount());
    pipeline->setDepthTest(true);
    pipeline->setDepthWrite(false);

    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    pipeline->setTargetBlends({blend});

    if (pipeline->create()) {
        outlinePipeline = std::move(pipeline);
    } else {
        qCCritical(lcRenderer) << "Cannot create outline pipeline";
    }
}

void QcWorldPassResources::releaseAtlasPipelines()
{
    solidPipeline.reset();
    cutoutPipeline.reset();
    translucentPipeline.reset();
    waterPipeline.reset();
    entityPipeline.reset();
    handPipeline.reset();
    atlasPipelinesTried = false;
}
