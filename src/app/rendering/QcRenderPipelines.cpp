// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/rendering/QcRenderPipelines.h"

#include "app/debug/QcAppLogging.h"
#include "app/rendering/QcDrawOrigins.h"
#include "clientcore/entitymeshes/QcEntityVertex.h"
#include "clientcore/meshing/QcBlockVertex.h"

#include <QFile>

#include <cstddef>

namespace {

// The parts the entity and the glow pipelines share: shaders, the entity vertex input, depth test,
// back faces culled.
std::unique_ptr<QRhiGraphicsPipeline> entityBase(QRhi *rhi, QRhiRenderTarget *target, QRhiShaderResourceBindings *bindings,
                                                 const QShader &vertexShader, const QShader &fragmentShader)
{
    std::unique_ptr<QRhiGraphicsPipeline> pipeline(rhi->newGraphicsPipeline());
    pipeline->setShaderStages({{QRhiShaderStage::Vertex, vertexShader}, {QRhiShaderStage::Fragment, fragmentShader}});
    pipeline->setVertexInputLayout(QcRenderPipelines::entityLayout());
    pipeline->setShaderResourceBindings(bindings);
    pipeline->setRenderPassDescriptor(target->renderPassDescriptor());
    pipeline->setSampleCount(target->sampleCount());
    pipeline->setDepthTest(true);
    pipeline->setFrontFace(QRhiGraphicsPipeline::CCW);
    pipeline->setCullMode(QRhiGraphicsPipeline::Back);
    return pipeline;
}

} // namespace

namespace QcRenderPipelines {

QRhiVertexInputLayout blockLayout()
{
    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({{sizeof(QcBlockVertex)}});
    inputLayout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float3, static_cast<quint32>(offsetof(QcBlockVertex, x))},
        {0, 1, QRhiVertexInputAttribute::Float2, static_cast<quint32>(offsetof(QcBlockVertex, u))},
        {0, 2, QRhiVertexInputAttribute::UNormByte4, static_cast<quint32>(offsetof(QcBlockVertex, light))},
    });
    // The section's corner, per draw (decision E-883).
    return QcDrawOrigins::withOrigins(inputLayout);
}

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
    std::unique_ptr<QRhiGraphicsPipeline> pipeline = entityBase(rhi, target, bindings, vertexShader, fragmentShader);
    pipeline->setDepthWrite(true);
    if (!pipeline->create()) {
        qCCritical(lcRenderer) << "Cannot create entity pipeline";
        return {};
    }
    return pipeline;
}

std::unique_ptr<QRhiGraphicsPipeline> createGlowPipeline(QRhi *rhi, QRhiRenderTarget *target, QRhiShaderResourceBindings *bindings,
                                                         const QShader &vertexShader, const QShader &fragmentShader)
{
    std::unique_ptr<QRhiGraphicsPipeline> pipeline = entityBase(rhi, target, bindings, vertexShader, fragmentShader);
    pipeline->setDepthWrite(false);
    QRhiGraphicsPipeline::TargetBlend add;
    add.enable = true;
    add.srcColor = QRhiGraphicsPipeline::One;
    add.dstColor = QRhiGraphicsPipeline::One;
    add.srcAlpha = QRhiGraphicsPipeline::Zero;
    add.dstAlpha = QRhiGraphicsPipeline::One;
    pipeline->setTargetBlends({add});
    if (!pipeline->create()) {
        qCCritical(lcRenderer) << "Cannot create glow pipeline";
        return {};
    }
    return pipeline;
}

} // namespace QcRenderPipelines
