#pragma once

#include "clientcore/meshing/QcChunkMesh.h"

#include <QByteArray>

#include <array>

// The lists of a section mesh (QcChunkMesh) packed for the GPU into one vertex and one index range
// (decision E-813), which the GPU mesh store places in a page shared with other sections (decision
// E-820). A list's indices stay relative to its own first vertex (a draw takes that vertex as base
// vertex, or binds the vertex buffer at the list's offset); they take two bytes when the list has at
// most 65536 vertices (QcIndexPacking), otherwise four, and every list starts at a multiple of four
// bytes in the index data (padding bytes are zero).
struct QcPackedMesh
{
    enum class List : quint8 { Opaque, Cutout, Translucent, Water, Underside };
    static constexpr int listCount = int(List::Underside) + 1;
    static constexpr int indexAlignment = 4;

    // Where one list lies in the packed data; indexCount 0 for an empty list.
    struct Place
    {
        quint32 vertexOffset = 0;
        quint32 indexOffset = 0;
        quint32 indexCount = 0;
        bool shortIndices = true;
    };

    QByteArray vertices;
    QByteArray indices;
    std::array<Place, listCount> places{};

    const Place &place(List list) const { return places[std::size_t(list)]; }
    bool isEmpty() const { return indices.isEmpty(); }

    static QcPackedMesh pack(const QcChunkMesh &mesh);
};
