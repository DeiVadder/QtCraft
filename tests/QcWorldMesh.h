// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/meshing/QcChunkMesher.h"

#include <QList>

// The mesh of a section with its vertices moved back into world coordinates, for tests that check
// positions in the world near the origin: the mesher places them relative to the section's corner
// (QcChunkMesh::origin, decision E-883).
inline QcChunkMesh qcWorldMesh(const QcMeshInput &input)
{
    QcChunkMesh mesh = QcChunkMesher::mesh(input);
    const auto x = static_cast<float>(mesh.origin.x);
    const auto y = static_cast<float>(mesh.origin.y);
    const auto z = static_cast<float>(mesh.origin.z);
    for (QList<QcBlockVertex> *list :
         {&mesh.opaqueVertices, &mesh.cutoutVertices, &mesh.translucentVertices, &mesh.waterVertices, &mesh.undersideVertices}) {
        for (QcBlockVertex &vertex : *list) {
            vertex.x += x;
            vertex.y += y;
            vertex.z += z;
        }
    }
    mesh.origin = {};
    return mesh;
}
