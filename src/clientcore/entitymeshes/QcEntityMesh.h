// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/entitymeshes/QcEntityVertex.h"
#include "common/core/QcVec3.h"

#include <QList>

#include <array>
#include <span>

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

    /*! Appends \a count quads at once, each with the triangles of addQuad, and returns their
        vertices (four per quad, at their defaults) for the caller to fill: the way to add the many
        quads of one object, valid until the next change of the mesh. */
    [[nodiscard]] std::span<QcEntityVertex> addQuads(qsizetype count)
    {
        const qsizetype firstVertex = vertices.size();
        const qsizetype firstIndex = indices.size();
        vertices.resize(firstVertex + count * 4);
        indices.resize(firstIndex + count * 6);

        const std::span<quint32> added(indices.data() + firstIndex, static_cast<std::size_t>(count) * 6);
        auto base = static_cast<quint32>(firstVertex);
        for (std::size_t index = 0; index < added.size(); index += 6, base += 4) {
            added[index] = base;
            added[index + 1] = base + 1;
            added[index + 2] = base + 2;
            added[index + 3] = base;
            added[index + 4] = base + 2;
            added[index + 5] = base + 3;
        }
        return {vertices.data() + firstVertex, static_cast<std::size_t>(count) * 4};
    }
};
