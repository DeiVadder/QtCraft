#pragma once

#include "QcBlockVertex.h"

#include <QList>

// Triangle mesh of one chunk section. Opaque and translucent geometry are drawn in separate passes.
struct QcChunkMesh
{
    QList<QcBlockVertex> opaqueVertices;
    QList<quint32> opaqueIndices;
    QList<QcBlockVertex> translucentVertices;
    QList<quint32> translucentIndices;

    bool isEmpty() const { return opaqueIndices.isEmpty() && translucentIndices.isEmpty(); }
};
