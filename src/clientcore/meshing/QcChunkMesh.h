#pragma once

#include "QcBlockVertex.h"

#include <QList>

// Triangle mesh of one chunk section. Opaque and translucent geometry are drawn in separate passes.
// The opaque faces towards the void below the world (the underside of the bottom layer, decision
// E-288) are kept apart: only a camera below y = 0 can see them, so the renderer skips them
// otherwise (one such face per column would otherwise add a draw call to every bottom section).
struct QcChunkMesh
{
    QList<QcBlockVertex> opaqueVertices;
    QList<quint32> opaqueIndices;
    QList<QcBlockVertex> translucentVertices;
    QList<quint32> translucentIndices;
    QList<QcBlockVertex> undersideVertices;
    QList<quint32> undersideIndices;

    bool isEmpty() const { return opaqueIndices.isEmpty() && translucentIndices.isEmpty() && undersideIndices.isEmpty(); }
};
