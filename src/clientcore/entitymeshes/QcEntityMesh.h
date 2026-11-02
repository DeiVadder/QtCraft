// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/entitymeshes/QcEntityVertex.h"
#include "common/core/QcVec3.h"

#include <QList>

#include <array>

/*! \brief Triangles of all entities of one frame (or of the first-person hand), alpha-tested.

    Vertex positions are relative to \c {origin} (decision E-883), which the builder sets before
    the first vertex near where the mesh lies (the camera, the eye): the floats stay small
    however far the world reaches, and the renderer places the origin relative to the camera
    (QcRenderOrigin). */
struct QcEntityMesh
{
    /*! Vertices, positions relative to \c {origin}. */
    QList<QcEntityVertex> vertices;
    /*! Indices into vertices, three per triangle. */
    QList<quint32> indices;
    /*! World position the vertex positions are relative to. */
    QcVec3 origin;

    /*! Returns a vertex at \a world relative to \c {origin}, subtracted in double; the rest at its
        defaults. */
    [[nodiscard]] QcEntityVertex vertexAt(const QcVec3 &world) const
    {
        QcEntityVertex vertex;
        vertex.x = static_cast<float>(world.x - origin.x);
        vertex.y = static_cast<float>(world.y - origin.y);
        vertex.z = static_cast<float>(world.z - origin.z);
        return vertex;
    }

    /*! Appends a quad: its four \a corners in order and the two triangles 0 1 2 and 0 2 3;
        \a bothSides also adds the two facing the other way. */
    void addQuad(const std::array<QcEntityVertex, 4> &corners, bool bothSides = false)
    {
        const auto base = static_cast<quint32>(vertices.size());
        for (const QcEntityVertex &corner : corners) {
            vertices.append(corner);
        }
        indices.append({base, base + 1, base + 2, base, base + 2, base + 3});
        if (bothSides) {
            indices.append({base, base + 2, base + 1, base, base + 3, base + 2});
        }
    }
};
