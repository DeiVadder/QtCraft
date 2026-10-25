#include "app/rendering/QcRenderPipelines.h"

#include "app/debug/QcAppLogging.h"
#include "app/rendering/QcDrawOrigins.h"
#include "clientcore/meshing/QcEntityVertex.h"

#include <QFile>

#include <cstddef>

namespace QcRenderPipelines {

namespace {

QRhiVertexInputLayout entityLayout()
{
    QRhiVertexInputLayout layout;
    layout.setBindings({{sizeof(QcEntityVertex)}});
    layout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float3, static_cast<quint32>(offsetof(QcEntityVertex, x))},
        {0, 1, QRhiVertexInputAttribute::Float2, static_cast<quint32>(offsetof(QcEntityVertex, u))},
        {0, 2, QRhiVertexInputAttribute::Float, static_cast<quint32>(offsetof(QcEntityVertex, shade))},
        {0, 3, QRhiVertexInputAttribute::Float, static_cast<quint32>(offsetof(QcEntityVertex, skyLight))},
        {0, 5, QRhiVertexInputAttribute::Float, static_cast<quint32>(offsetof(QcEntityVertex, blockLight))},
        {0, 4, QRhiVertexInputAttribute::UNormByte4, static_cast<quint32>(offsetof(QcEntityVertex, tint))},
    });
    // The mesh's origin, per draw (decision E-883).
    return QcDrawOrigins::withOrigins(layout);
}

} // namespace

QShader loadShader(const QString &path, const QLoggingCategory &category)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCCritical(category) << "Cannot open shader" << path;
        return {};
    }
    return QShader::fromSerialized(file.readAll());
}

std::unique_ptr<QRhiGraphicsPipeline> createEntityPipeline(QRhi *rhi, QRhiRenderTarget *target, QRhiShaderResourceBindings *bindings,
                                                           const QShader &vertexShader, const QShader &fragmentShader)
{
    std::unique_ptr<QRhiGraphicsPipeline> pipeline(rhi->newGraphicsPipeline());
    pipeline->setShaderStages({{QRhiShaderStage::Vertex, vertexShader}, {QRhiShaderStage::Fragment, fragmentShader}});
    pipeline->setVertexInputLayout(entityLayout());
    pipeline->setShaderResourceBindings(bindings);
    pipeline->setRenderPassDescriptor(target->renderPassDescriptor());
    pipeline->setSampleCount(target->sampleCount());
    pipeline->setDepthTest(true);
    pipeline->setDepthWrite(true);
    pipeline->setFrontFace(QRhiGraphicsPipeline::CCW);
    pipeline->setCullMode(QRhiGraphicsPipeline::Back);

    if (!pipeline->create()) {
        qCCritical(lcRenderer) << "Cannot create entity pipeline";
        return {};
    }
    return pipeline;
}

} // namespace QcRenderPipelines
