#include "app/rendering/QcPrecipitationRenderer.h"

#include "app/debug/QcAppLogging.h"
#include "app/rendering/QcRenderPipelines.h"


#include <array>
#include <cstddef>

using namespace Qt::StringLiterals;
using QcRenderPipelines::loadShader;

namespace {

// The corners of a particle's quad as two triangles: x across the streak or dot, y along it
// (-1 at the drop, 1 at the far end of the streak).
constexpr std::array<float, 12> quadCorners = {-1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f};

// The Shape block of precipitation.vert (std140: four vec4).
struct ShapeUniforms
{
    float streak[4];
    float flake[4];
    float rainColor[4];
    float snowColor[4];
};

ShapeUniforms shapeUniforms()
{
    const QcVec3 streak = QcPrecipitationField::rainStreak();
    // At 1080 pixels high: a streak two pixels wide; a flake between three and nine pixels across.
    return {{static_cast<float>(streak.x), static_cast<float>(streak.y), static_cast<float>(streak.z), 1.0f},
            {static_cast<float>(QcPrecipitationField::flakeSize), 1.5f, 4.5f, 0.0f},
            // Grey-blue streaks of rain, white flakes, both in full daylight.
            {0.82f, 0.88f, 0.98f, 1.0f},
            {0.97f, 0.98f, 1.0f, 1.0f}};
}

} // namespace

void QcPrecipitationRenderer::reset()
{
    m_pipeline.reset();
    m_bindings.reset();
    m_shape.reset();
    m_instances.reset();
    m_corners.reset();
    m_instanceCount = 0;
}

void QcPrecipitationRenderer::prepare(QRhi *rhi, QRhiRenderTarget *target, QRhiResourceUpdateBatch *updates, QRhiBuffer *frameUniforms,
                                      const std::shared_ptr<const QList<QcPrecipitationDrop>> &drops)
{
    m_instanceCount = 0;
    if (!drops || drops->isEmpty()) {
        return;
    }
    if (!m_pipeline) {
        createResources(rhi, target, updates, frameUniforms);
    }
    if (!m_pipeline) {
        return;
    }

    // A dynamic buffer that grows on demand and is reused every frame; the drops go up as they are.
    const auto bytes = static_cast<quint32>(drops->size() * static_cast<qsizetype>(sizeof(QcPrecipitationDrop)));
    if (!m_instances || m_instances->size() < bytes) {
        m_instances.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, bytes * 2));
        m_instances->create();
    }
    updates->updateDynamicBuffer(m_instances.get(), 0, bytes, drops->constData());
    // 64 bytes; set every frame, as a dynamic buffer has one copy per frame in flight.
    static const ShapeUniforms shape = shapeUniforms();
    updates->updateDynamicBuffer(m_shape.get(), 0, static_cast<quint32>(sizeof(ShapeUniforms)), &shape);
    m_instanceCount = static_cast<quint32>(drops->size());
}

bool QcPrecipitationRenderer::draw(QRhiCommandBuffer *commandBuffer) const
{
    if (m_instanceCount == 0 || !m_pipeline) {
        return false;
    }

    commandBuffer->setGraphicsPipeline(m_pipeline.get());
    commandBuffer->setShaderResources();
    const std::array<QRhiCommandBuffer::VertexInput, 2> inputs = {QRhiCommandBuffer::VertexInput(m_corners.get(), 0),
                                                                 QRhiCommandBuffer::VertexInput(m_instances.get(), 0)};
    commandBuffer->setVertexInput(0, static_cast<int>(inputs.size()), inputs.data());
    commandBuffer->draw(static_cast<quint32>(quadCorners.size() / 2), m_instanceCount);
    return true;
}

void QcPrecipitationRenderer::createResources(QRhi *rhi, QRhiRenderTarget *target, QRhiResourceUpdateBatch *updates,
                                              QRhiBuffer *frameUniforms)
{
    m_corners.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, static_cast<quint32>(sizeof(quadCorners))));
    m_corners->create();
    updates->uploadStaticBuffer(m_corners.get(), quadCorners.data());
    m_shape.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, static_cast<quint32>(sizeof(ShapeUniforms))));
    m_shape->create();

    m_bindings.reset(rhi->newShaderResourceBindings());
    const auto stages = QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage;
    m_bindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, stages, frameUniforms),
                             QRhiShaderResourceBinding::uniformBuffer(1, QRhiShaderResourceBinding::VertexStage, m_shape.get())});
    m_bindings->create();

    std::unique_ptr<QRhiGraphicsPipeline> pipeline(rhi->newGraphicsPipeline());
    pipeline->setShaderStages({{QRhiShaderStage::Vertex, loadShader(u":/shaders/precipitation.vert.qsb"_s, lcRenderer())},
                               {QRhiShaderStage::Fragment, loadShader(u":/shaders/precipitation.frag.qsb"_s, lcRenderer())}});
    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({{2 * sizeof(float)}, {sizeof(QcPrecipitationDrop), QRhiVertexInputBinding::PerInstance}});
    inputLayout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0},
                               {1, 1, QRhiVertexInputAttribute::Float3, static_cast<quint32>(offsetof(QcPrecipitationDrop, x))},
                               {1, 2, QRhiVertexInputAttribute::UNormByte4, static_cast<quint32>(offsetof(QcPrecipitationDrop, snow))}});
    pipeline->setVertexInputLayout(inputLayout);
    pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
    pipeline->setCullMode(QRhiGraphicsPipeline::None);
    pipeline->setShaderResourceBindings(m_bindings.get());
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
        m_pipeline = std::move(pipeline);
    } else {
        qCCritical(lcRenderer) << "Cannot create precipitation pipeline";
    }
}
