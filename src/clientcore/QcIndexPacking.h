#pragma once

#include <QByteArray>
#include <QList>

// Index buffers of section meshes: two bytes per index whenever every vertex can be addressed
// with 16 bits, otherwise four.
namespace QcIndexPacking {

// Largest vertex count whose indices (0 .. count - 1) fit into 16 bits.
inline constexpr qsizetype maxShortIndexVertices = 65536;

constexpr bool fitsShortIndices(qsizetype vertexCount)
{
    return vertexCount <= maxShortIndexVertices;
}

// `indices` as consecutive native-endian quint16 values; only valid when fitsShortIndices() holds
// for the mesh the indices belong to.
QByteArray toShortIndices(const QList<quint32> &indices);

} // namespace QcIndexPacking
