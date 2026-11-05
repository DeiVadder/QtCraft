// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/meshing/QcChunkMesh.h"

#include <QByteArray>

#include <array>

/*! \brief The lists of a section mesh (QcChunkMesh) packed for the GPU into one vertex and one
    index range (decision E-813), which the GPU mesh store places in a page shared with other
    sections (decision E-820).

    A list's indices stay relative to its own first vertex (a draw takes that vertex as base
    vertex, or binds the vertex buffer at the list's offset); they take two bytes when the list
    has at most 65536 vertices (QcIndexPacking), otherwise four, and every list starts at a
    multiple of four bytes in the index data (padding bytes are zero). */
struct QcPackedMesh
{
    /*! One list of a section mesh, in the order of the packed data. */
    enum class List : quint8 {
        Opaque,      // QcChunkMesh::opaqueVertices
        Cutout,      // QcChunkMesh::cutoutVertices
        Translucent, // QcChunkMesh::translucentVertices
        Water,       // QcChunkMesh::waterVertices
        Underside    // QcChunkMesh::undersideVertices
    };
    /*! Number of lists. */
    static constexpr int listCount = static_cast<int>(List::Underside) + 1;
    /*! Every list starts at a multiple of this many bytes in the index data. */
    static constexpr int indexAlignment = 4;

    /*! \brief Where one list lies in the packed data; indexCount 0 for an empty list. */
    struct Place
    {
        /*! Offset of the list's first vertex in vertices, in bytes. */
        quint32 vertexOffset = 0;
        /*! Offset of the list's first index in indices, in bytes. */
        quint32 indexOffset = 0;
        /*! Number of indices of the list. */
        quint32 indexCount = 0;
        /*! Two-byte indices; else four. */
        bool shortIndices = true;
    };

    /*! Vertex data of all lists (QcBlockVertex). */
    QByteArray vertices;
    /*! Index data of all lists, each list aligned to indexAlignment. */
    QByteArray indices;
    /*! Where each list lies, indexed by List. */
    std::array<Place, listCount> places{};

    /*! Returns where \a list lies in the packed data. */
    const Place &place(List list) const { return places[static_cast<std::size_t>(list)]; }
    /*! Returns whether no list has an index. */
    bool isEmpty() const { return indices.isEmpty(); }

    /*! Returns \a mesh packed into one vertex and one index range. */
    static QcPackedMesh pack(const QcChunkMesh &mesh);
};
