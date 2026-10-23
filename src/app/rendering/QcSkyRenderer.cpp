#include "app/rendering/QcSkyRenderer.h"

#include "app/debug/QcAppLogging.h"
#include "app/rendering/QcRenderPipelines.h"


#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;
using QcRenderPipelines::loadShader;

namespace {

// std140 layout of the Sky uniform block in sky.vert and sky.frag.
struct SkyUniforms
{
    float inverseViewProjection[16];
    float zenith[4];
    float horizon[4];
    float glow[4];
    float sunDirection[4];
    float moonDirection[4];
    float cloudColor[4];
    float params[4];
    float camera[4];
};
static_assert(sizeof(SkyUniforms) == 192);

void copy(QVector3D value, float w, float (&target)[4])
{
    target[0] = value.x();
    target[1] = value.y();
    target[2] = value.z();
    target[3] = w;
}

} // namespace

void QcSkyRenderer::reset()
{
    m_pipeline.reset();
    m_bindings.reset();
    m_uniforms.reset();
}

void QcSkyRenderer::createPipeline(QRhi *rhi, QRhiRenderTarget *target)
{
    m_uniforms.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(SkyUniforms)));
    m_uniforms->create();
    m_bindings.reset(rhi->newShaderResourceBindings());
    m_bindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(
        0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_uniforms.get())});
    m_bindings->create();

    std::unique_ptr<QRhiGraphicsPipeline> pipeline(rhi->newGraphicsPipeline());
    pipeline->setShaderStages({{QRhiShaderStage::Vertex, loadShader(u":/shaders/sky.vert.qsb"_s, lcRenderer())},
                               {QRhiShaderStage::Fragment, loadShader(u":/shaders/sky.frag.qsb"_s, lcRenderer())}});
    // No vertex buffer: the shader makes the triangle from the vertex index.
    pipeline->setVertexInputLayout({});
    pipeline->setShaderResourceBindings(m_bindings.get());
    pipeline->setRenderPassDescriptor(target->renderPassDescriptor());
    pipeline->setSampleCount(target->sampleCount());
    pipeline->setDepthTest(false);
    pipeline->setDepthWrite(false);
    if (pipeline->create()) {
        m_pipeline = std::move(pipeline);
    } else {
        qCCritical(lcRenderer) << "Cannot create sky pipeline";
    }
}

void QcSkyRenderer::prepare(QRhi *rhi, QRhiRenderTarget *target, QRhiResourceUpdateBatch *updates, const Frame &frame)
{
    if (!m_pipeline) {
        createPipeline(rhi, target);
    }
    if (!m_pipeline) {
        return;
    }

    const QcSkyState &sky = frame.sky;
    SkyUniforms uniforms{};
    const QMatrix4x4 inverse = frame.viewRotation.inverted();
    std::copy_n(inverse.constData(), 16, uniforms.inverseViewProjection);
    copy(sky.zenith, 1.0f, uniforms.zenith);
    copy(sky.horizon, 1.0f, uniforms.horizon);
    copy(sky.glow, 1.0f, uniforms.glow);
    copy(sky.sunDirection, 0.0f, uniforms.sunDirection);
    copy(sky.moonDirection, static_cast<float>(sky.moonPhase), uniforms.moonDirection);
    copy(sky.cloudColor, static_cast<float>(sky.overcast), uniforms.cloudColor);
    const float starAngle = std::atan2(sky.sunDirection.y(), sky.sunDirection.x());
    // Offsets wrap after 100 km so the floats keep their precision in long sessions.
    uniforms.params[0] = static_cast<float>(sky.stars);
    uniforms.params[1] = starAngle;
    uniforms.params[2] = static_cast<float>(std::fmod(sky.cloudOffsetX, 100000.0));
    uniforms.params[3] = static_cast<float>(std::fmod(sky.cloudOffsetZ, 100000.0));
    copy(frame.camera, static_cast<float>(QcSky::cloudHeight), uniforms.camera);
    updates->updateDynamicBuffer(m_uniforms.get(), 0, sizeof(SkyUniforms), &uniforms);
}

void QcSkyRenderer::draw(QRhiCommandBuffer *commandBuffer) const
{
    if (!m_pipeline) {
        return;
    }
    commandBuffer->setGraphicsPipeline(m_pipeline.get());
    commandBuffer->setShaderResources();
    commandBuffer->draw(3);
}
