#include "app/rendering/QcDebugLineRenderer.h"

#include "app/rendering/QcRenderPipelines.h"

#include <QLoggingCategory>

#include <cstddef>

using namespace Qt::StringLiterals;
using QcRenderPipelines::loadShader;

namespace {

Q_LOGGING_CATEGORY(lcRenderer, "qtcraft.renderer")

} // namespace

void QcDebugLineRenderer::reset()
{
    m_pipeline.reset();
    m_bindings.reset();
    m_vertices.reset();
    m_vertexCount = 0;
}

void QcDebugLineRenderer::prepare(QRhi *rhi, QRhiRenderTarget *target, QRhiResourceUpdateBatch *updates, QRhiBuffer *frameUniforms,
                                  const std::shared_ptr<const QList<QcDebugLineVertex>> &lines)
{
    m_vertexCount = 0;
    if (!lines || lines->isEmpty()) {
        return;
    }
    if (!m_pipeline) {
        createPipeline(rhi, target, frameUniforms);
    }
    if (!m_pipeline) {
        return;
    }

    // Two triangles per segment, each corner knowing the other end.
    m_corners.clear();
    m_corners.reserve(lines->size() * 3);
    for (qsizetype i = 0; i + 1 < lines->size(); i += 2) {
        const QcDebugLineVertex &a = lines->at(i);
        const QcDebugLineVertex &b = lines->at(i + 1);
        // The shader widens by `side` times three pixels.
        const auto corner = [](QcDebugLineVertex end, QcDebugLineVertex other, float side) {
            return Corner{{end.x, end.y, end.z}, {other.x, other.y, other.z}, side, {end.rgba[0], end.rgba[1], end.rgba[2], end.rgba[3]}};
        };
        m_corners.append({corner(a, b, -1.0f), corner(a, b, 1.0f), corner(b, a, 1.0f)});
        m_corners.append({corner(a, b, -1.0f), corner(b, a, 1.0f), corner(b, a, -1.0f)});
    }

    // A dynamic buffer that grows on demand and is reused every frame.
    const auto bytes = quint32(m_corners.size() * qsizetype(sizeof(Corner)));
    if (!m_vertices || m_vertices->size() < bytes) {
        m_vertices.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, bytes * 2));
        m_vertices->create();
    }
    updates->updateDynamicBuffer(m_vertices.get(), 0, bytes, m_corners.constData());
    m_vertexCount = quint32(m_corners.size());
}

bool QcDebugLineRenderer::draw(QRhiCommandBuffer *commandBuffer) const
{
    if (m_vertexCount == 0 || !m_pipeline) {
        return false;
    }

    commandBuffer->setGraphicsPipeline(m_pipeline.get());
    commandBuffer->setShaderResources();
    const QRhiCommandBuffer::VertexInput input(m_vertices.get(), 0);
    commandBuffer->setVertexInput(0, 1, &input);
    commandBuffer->draw(m_vertexCount);
    return true;
}

void QcDebugLineRenderer::createPipeline(QRhi *rhi, QRhiRenderTarget *target, QRhiBuffer *frameUniforms)
{
    m_bindings.reset(rhi->newShaderResourceBindings());
    m_bindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, frameUniforms)});
    m_bindings->create();

    std::unique_ptr<QRhiGraphicsPipeline> pipeline(rhi->newGraphicsPipeline());
    pipeline->setShaderStages({{QRhiShaderStage::Vertex, loadShader(u":/shaders/debugline.vert.qsb"_s, lcRenderer())},
                               {QRhiShaderStage::Fragment, loadShader(u":/shaders/debugline.frag.qsb"_s, lcRenderer())}});
    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({{sizeof(Corner)}});
    inputLayout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, quint32(offsetof(Corner, position))},
                               {0, 1, QRhiVertexInputAttribute::Float3, quint32(offsetof(Corner, other))},
                               {0, 2, QRhiVertexInputAttribute::Float, quint32(offsetof(Corner, side))},
                               {0, 3, QRhiVertexInputAttribute::UNormByte4, quint32(offsetof(Corner, rgba))}});
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
        qCCritical(lcRenderer) << "Cannot create debug line pipeline";
    }
}
