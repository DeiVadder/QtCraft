// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/meshing/QcBlockRenderTable.h"
#include "clientcore/meshing/QcBlockVertex.h"

#include <QList>

#include <array>
#include <optional>

/*! \brief Shading, sky light and block light given to all four vertices of a quad.
    \qcfields */
struct QcQuadLight
{
    float shade = 1.0f;
    float sky = 0.0f;
    float block = 0.0f;
};

/*! \brief Normal and material written into the surface byte of a quad's vertices (decision
    E-390). \qcfields */
struct QcQuadSurface
{
    std::optional<QcBlockFace> normal;
    QcSurfaceMaterial material = QcSurfaceMaterial::Solid;
};

/*! \brief Vertex and index lists of one render layer of a chunk mesh, which the chunk mesher
    appends its quads to.

    Water and the other translucent faces also note the order in which they alternate
    (QcChunkMesh::translucentRuns). */
struct QcMeshOutput
{
    /*! The runs a layer's faces belong to; the value is the parity of their runs in
        QcChunkMesh::translucentRuns (water first). */
    enum class Run : quint8
    {
        /*! Faces of translucent water. */
        Water = 0,
        /*! Every other translucent face. */
        OtherTranslucent = 1,
    };

    /*! The vertices of the layer. */
    QList<QcBlockVertex> &vertices;
    /*! The triangle indices of the layer. */
    QList<quint32> &indices;
    /*! The translucent runs to note the quads in, or null for a layer without them. */
    QList<quint32> *runs = nullptr;
    /*! The kind of run the quads belong to (with \c {runs} only). */
    Run run = Run::Water;

    /*! Appends the quad \a corners with \a surface written into each vertex. Its two triangles
        meet along the diagonal from corner 0 to 2, or from 1 to 3 when \a flipDiagonal is set. */
    void appendQuad(const std::array<QcBlockVertex, 4> &corners, bool flipDiagonal, QcQuadSurface surface) const;

    /*! Appends two quads along the diagonals of \a cross in the cell whose corner lies at
        \a local in the section, each with a front and a back side so back-face culling keeps
        both visible, lit by \a light. Without a normal; plants unless \a material says more
        (light sources). */
    void appendCross(const QcBlockRenderTable::Cross &cross, QcBlockPos local, QcQuadLight light,
                     QcSurfaceMaterial material) const;

private:
    void noteRun() const;
};
