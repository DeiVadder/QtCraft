#include "app/rendering/QcWorldRendererResources.h"

#include "app/debug/QcAppLogging.h"
#include "app/rendering/QcAtlasTexture.h"
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

    // Adds the edges of `box`; false (and nothing added) when maxOutlineBoxes boxes are in.
    [[nodiscard]] bool addBox(const QcAabb &box);

    const float *data() const { return m_values.data(); }
    // Number of floats written so far.
    std::size_t size() const { return m_size; }

private:
    std::array<float, static_cast<std::size_t>(maxOutlineBoxes) * floatsPerBox> m_values{};
    std::size_t m_size = 0;
};

bool OutlineVertices::addBox(const QcAabb &box)
{
    if (m_size + floatsPerBox > m_values.size()) {
        return false;
    }

    // Slightly larger than the boxes so the lines are not hidden by their faces.
    constexpr float grow = 0.004f;
    constexpr std::array<std::pair<int, int>, 12> edges = {
        {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};

    const float x0 = static_cast<float>(box.min.x) - grow;
    const float y0 = static_cast<float>(box.min.y) - grow;
    const float z0 = static_cast<float>(box.min.z) - grow;
    const float x1 = static_cast<float>(box.max.x) + grow;
    const float y1 = static_cast<float>(box.max.y) + grow;
    const float z1 = static_cast<float>(box.max.z) + grow;
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

QRhiVertexInputLayout blockVertexLayout()
{
    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({{sizeof(QcBlockVertex)}});
    inputLayout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float3, static_cast<quint32>(offsetof(QcBlockVertex, x))},
        {0, 1, QRhiVertexInputAttribute::Float2, static_cast<quint32>(offsetof(QcBlockVertex, u))},
        {0, 2, QRhiVertexInputAttribute::UNormByte4, static_cast<quint32>(offsetof(QcBlockVertex, light))},
    });
    return inputLayout;
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

void QcWorldDeviceResources::uploadOutline(QRhiResourceUpdateBatch *updates, const QList<QcAabb> &selection)
{
    if (selection.isEmpty() || selection == uploadedSelection || !outlineVertices) {
        return;
    }

    // The outline shows the first maxOutlineBoxes boxes of a shape.
    OutlineVertices vertices;
    for (const QcAabb &box : selection) {
        if (!vertices.addBox(box)) {
            break;
        }
    }

    updates->updateDynamicBuffer(outlineVertices.get(), 0, static_cast<quint32>(vertices.size() * sizeof(float)), vertices.data());
    outlineVertexCount = static_cast<quint32>(vertices.size() / 3);
    uploadedSelection = selection;
}

bool QcWorldPassResources::fits(const QRhiRenderTarget *target) const
{
    return renderPass && sampleCount == target->sampleCount() && renderPass->isCompatible(target->renderPassDescriptor());
}

void QcWorldPassResources::createAtlasPipelines(QRhi *rhi, QRhiRenderTarget *target, QRhiShaderResourceBindings *atlasBindings)
{
    atlasPipelinesTried = true;
    const BlockPipelines blocks{rhi, target, atlasBindings, loadShader(u":/shaders/block.vert.qsb"_s, lcRenderer()), blockVertexLayout()};
    const QShader fragmentShader = loadShader(u":/shaders/block.frag.qsb"_s, lcRenderer());
    // The opaque pass writes alpha 1 (decision E-332): mip levels give cut-out texels an alpha
    // between 0 and 1, which a screenshot or a transparent target would read as half transparent. A
    // colour write mask without alpha did the same but cost up to a third more GPU time. Its solid
    // faces take a shader without `discard`, so the GPU shades only the fragments that stay visible
    // (decision E-756); the cut-out faces keep the alpha test.
    solidPipeline = blocks.build(loadShader(u":/shaders/block_solid.frag.qsb"_s, lcRenderer()), false);
    cutoutPipeline = blocks.build(loadShader(u":/shaders/block_opaque.frag.qsb"_s, lcRenderer()), false);
    translucentPipeline = blocks.build(fragmentShader, true);
    waterPipeline = blocks.build(fragmentShader, true);

    const QShader entityFragment = loadShader(u":/shaders/entity.frag.qsb"_s, lcRenderer());
    entityPipeline = QcRenderPipelines::createEntityPipeline(rhi, target, atlasBindings,
                                                             loadShader(u":/shaders/entity.vert.qsb"_s, lcRenderer()), entityFragment);
    handPipeline = QcRenderPipelines::createEntityPipeline(rhi, target, atlasBindings,
                                                           loadShader(u":/shaders/hand.vert.qsb"_s, lcRenderer()), entityFragment);
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
