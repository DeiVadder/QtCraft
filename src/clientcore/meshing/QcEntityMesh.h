#pragma once

#include "clientcore/meshing/QcEntityVertex.h"

#include <QList>

#include <array>

// Triangles of all entities of one frame (or of the first-person hand), alpha-tested.
struct QcEntityMesh
{
    QList<QcEntityVertex> vertices;
    QList<quint32> indices;

    // Appends a quad: its four corners in order and the two triangles 0 1 2 and 0 2 3; `bothSides`
    // also adds the two facing the other way.
    void addQuad(const std::array<QcEntityVertex, 4> &corners, bool bothSides = false)
    {
        const auto base = quint32(vertices.size());
        for (const QcEntityVertex &corner : corners) {
            vertices.append(corner);
        }
        indices.append({base, base + 1, base + 2, base, base + 2, base + 3});
        if (bothSides) {
            indices.append({base, base + 2, base + 1, base, base + 3, base + 2});
        }
    }
};
