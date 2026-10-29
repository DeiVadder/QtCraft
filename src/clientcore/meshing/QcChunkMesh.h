// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/light/QcSectionLight.h"
#include "clientcore/meshing/QcBlockVertex.h"
#include "common/core/QcBlockPos.h"

#include <QList>

// Triangle mesh of one chunk section. Opaque and translucent geometry are drawn in separate passes.
// Within the opaque pass the cut-out faces (leaves, plants, torches: QcRenderLayer::Cutout, texels
// discarded by alpha) have lists of their own, so the solid faces are drawn by a pipeline without
// `discard`, which lets the GPU reject hidden fragments before shading them (decision E-756).
// Translucent liquids (water) have lists of their own, apart from the other translucent faces (glass,
// ice), so a render mode can give water its own pipeline (decision E-390); today's renderer blends
// both in one back-to-front pass, within a section in the order the mesher emitted them
// (translucentRuns). The opaque faces towards the void below the world (the underside of the
// bottom layer, decision E-288) are kept apart: only a camera below y = 0 can see them, so the
// renderer skips them otherwise (one such face per column would otherwise add a draw call to every
// bottom section).
//
// Vertex positions are relative to `origin`, the section's bottom north-west corner (decision
// E-883): from 0 to 16 along each axis, exact in a float wherever the section lies; the renderer
// places the origin relative to the camera (QcRenderOrigin).
struct QcChunkMesh
{
    // The corner of the section in the world: chunk x * 16, section index * 16, chunk z * 16.
    QcBlockPos origin;
    QList<QcBlockVertex> opaqueVertices;
    QList<quint32> opaqueIndices;
    QList<QcBlockVertex> cutoutVertices;
    QList<quint32> cutoutIndices;
    QList<QcBlockVertex> translucentVertices;
    QList<quint32> translucentIndices;
    QList<QcBlockVertex> waterVertices;
    QList<quint32> waterIndices;
    // Where water and the other translucent faces alternate in the order the mesher emitted them
    // (cell by cell): index counts of runs, water first (possibly 0), then other translucent faces,
    // and so on. Empty when that order is all water before all other translucent faces; drawing the
    // runs in turn blends coplanar and overlapping faces exactly as one mixed list would.
    QList<quint32> translucentRuns;
    QList<QcBlockVertex> undersideVertices;
    QList<quint32> undersideIndices;
    // Light of the section's cells (decision E-390), for shaders that read light per cell; set for
    // every mesh that has faces when the input asks for it (QcMeshInput::sectionLight).
    QcSectionLight light;

    bool isEmpty() const
    {
        return opaqueIndices.isEmpty() && cutoutIndices.isEmpty() && translucentIndices.isEmpty() && waterIndices.isEmpty()
               && undersideIndices.isEmpty();
    }
};
