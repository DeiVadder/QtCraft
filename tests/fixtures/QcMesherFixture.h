// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/meshing/QcChunkMesher.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/registry/QcContentRegistry.h"

#include <QList>
#include <QString>

#include <memory>
#include <optional>

// Blocks and inputs for the mesher tests: a few made-up blocks of each render layer in one chunk
// with empty neighbours.

inline QcBlockDefinition makeBlock(const QString &name, QcRenderLayer layer, bool opaque, bool liquid = false)
{
    QcBlockDefinition block;
    block.name = name;
    block.renderLayer = layer;
    block.opaque = opaque;
    block.solid = !liquid;
    block.liquid = liquid;
    block.faceTextures.fill(name);
    return block;
}

struct QcMesherFixture
{
    QcContentRegistry content;
    QcBlockId stone = 0;
    QcBlockId water = 0;
    QcBlockId leaves = 0;
    QcBlockId glass = 0;
    QcBlockId sapling = 0;
    QcBlockId ice = 0;
    // A translucent full cube lying along x: its north and south sides show their texture turned.
    QcBlockId glassLogAlongX = 0;
    std::shared_ptr<const QcBlockRenderTable> table;

    QcMesherFixture()
    {
        stone = *content.registerBlock(makeBlock(QStringLiteral("t:stone"), QcRenderLayer::Opaque, true));
        water = *content.registerBlock(makeBlock(QStringLiteral("t:water"), QcRenderLayer::Translucent, false, true));
        leaves = *content.registerBlock(makeBlock(QStringLiteral("t:leaves"), QcRenderLayer::Cutout, false));
        glass = *content.registerBlock(makeBlock(QStringLiteral("t:glass"), QcRenderLayer::Translucent, false));
        QcBlockDefinition cross = makeBlock(QStringLiteral("t:sapling"), QcRenderLayer::Cutout, false, true);
        cross.liquid = false;
        cross.shape.parts = {[] {
            QcShapePart part;
            part.crosses = {{{0.15, 0.0, 0.15}, {0.85, 1.0, 0.85}}};
            return part;
        }()};
        sapling = *content.registerBlock(cross);
        ice = *content.registerBlock(makeBlock(QStringLiteral("t:ice"), QcRenderLayer::Translucent, false));
        QcBlockDefinition log = makeBlock(QStringLiteral("t:glass_log"), QcRenderLayer::Translucent, false);
        log.properties = {QcBlockProperty{QStringLiteral("axis"), {QStringLiteral("y"), QStringLiteral("x"), QStringLiteral("z")}}};
        log.shape.rotate = QStringLiteral("axis");
        log.shape.parts = {[] {
            QcShapePart part;
            part.boxes = {{{0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}}};
            return part;
        }()};
        glassLogAlongX = content.withProperty(*content.registerBlock(log), QStringLiteral("axis"), QStringLiteral("x"));
        table = std::make_shared<const QcBlockRenderTable>(content, QcTextureAtlas::build(content));
    }

    // Centre chunk at (0, 0) plus empty neighbours, unless `withNeighbours` is false.
    QcMeshInput input(const std::shared_ptr<QcChunk> &centre, bool withNeighbours = true) const
    {
        QcMeshInput in;
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dz == 0) {
                    in.chunks[4] = centre;
                } else if (withNeighbours) {
                    const int index = (dz + 1) * 3 + (dx + 1);
                    in.chunks[static_cast<std::size_t>(index)] = std::make_shared<QcChunk>(QcChunkPos{dx, dz});
                }
            }
        }
        in.renderTable = table;
        return in;
    }
};

inline int quadCount(const QList<quint32> &indices)
{
    return static_cast<int>(indices.size() / 6);
}

// Quads of a vertex list that face `face`.
inline int facesTowards(const QList<QcBlockVertex> &vertices, QcBlockFace face)
{
    int count = 0;
    for (qsizetype i = 0; i < vertices.size(); i += 4) {
        count += vertices[i].normal() == std::optional<QcBlockFace>(face) ? 1 : 0;
    }
    return count;
}

