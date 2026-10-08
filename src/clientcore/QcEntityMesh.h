#pragma once

#include "QcEntityVertex.h"

#include <QList>

// Triangles of all entities of one frame (or of the first-person hand), alpha-tested.
struct QcEntityMesh
{
    QList<QcEntityVertex> vertices;
    QList<quint32> indices;
};
