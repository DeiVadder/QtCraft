// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/meshing/QcPackedMesh.h"

#include "clientcore/meshing/QcIndexPacking.h"

#include <cstring>

namespace {

struct Source
{
    const QList<QcBlockVertex> *vertices = nullptr;
    const QList<quint32> *indices = nullptr;
};

// The lists in the order of QcPackedMesh::List.
std::array<Source, QcPackedMesh::listCount> sources(const QcChunkMesh &mesh)
{
    return {Source{&mesh.opaqueVertices, &mesh.opaqueIndices}, Source{&mesh.cutoutVertices, &mesh.cutoutIndices},
            Source{&mesh.translucentVertices, &mesh.translucentIndices}, Source{&mesh.waterVertices, &mesh.waterIndices},
            Source{&mesh.undersideVertices, &mesh.undersideIndices}};
}

qsizetype aligned(qsizetype bytes)
{
    constexpr qsizetype alignment = QcPackedMesh::indexAlignment;
    return (bytes + alignment - 1) / alignment * alignment;
}

void copyIndices(char *out, const QList<quint32> &indices, bool shortIndices)
{
    if (!shortIndices) {
        std::memcpy(out, indices.constData(), static_cast<std::size_t>(indices.size()) * sizeof(quint32));
        return;
    }
    for (const quint32 index : indices) {
        const auto value = static_cast<quint16>(index);
        std::memcpy(out, &value, sizeof(value));
        out += sizeof(value);
    }
}

} // namespace

QcPackedMesh QcPackedMesh::pack(const QcChunkMesh &mesh)
{
    const std::array<Source, listCount> lists = sources(mesh);
    QcPackedMesh packed;
    qsizetype vertexBytes = 0;
    qsizetype indexBytes = 0;
    for (std::size_t list = 0; list < lists.size(); ++list) {
        const Source &source = lists[list];
        if (source.indices->isEmpty()) {
            continue;
        }
        Place &place = packed.places[list];
        place.shortIndices = QcIndexPacking::fitsShortIndices(source.vertices->size());
        place.vertexOffset = static_cast<quint32>(vertexBytes);
        place.indexOffset = static_cast<quint32>(indexBytes);
        place.indexCount = static_cast<quint32>(source.indices->size());
        vertexBytes += source.vertices->size() * static_cast<qsizetype>(sizeof(QcBlockVertex));
        indexBytes = aligned(indexBytes + source.indices->size() * static_cast<qsizetype>(place.shortIndices ? sizeof(quint16) : sizeof(quint32)));
    }

    // Each list is written exactly once; only the index padding needs zeroing.
    packed.vertices.resize(vertexBytes);
    packed.indices = QByteArray(indexBytes, '\0');
    for (std::size_t list = 0; list < lists.size(); ++list) {
        const Place &place = packed.places[list];
        if (place.indexCount == 0) {
            continue;
        }
        const Source &source = lists[list];
        std::memcpy(packed.vertices.data() + place.vertexOffset, source.vertices->constData(),
                    static_cast<std::size_t>(source.vertices->size()) * sizeof(QcBlockVertex));
        copyIndices(packed.indices.data() + place.indexOffset, *source.indices, place.shortIndices);
    }
    return packed;
}
