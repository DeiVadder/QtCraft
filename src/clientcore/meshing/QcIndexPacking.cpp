#include "clientcore/meshing/QcIndexPacking.h"

#include <QtGlobal>

namespace QcIndexPacking {

QByteArray toShortIndices(const QList<quint32> &indices)
{
    QByteArray packed(indices.size() * qsizetype(sizeof(quint16)), Qt::Uninitialized);
    auto *out = reinterpret_cast<quint16 *>(packed.data());
    for (const quint32 index : indices) {
        Q_ASSERT(index < maxShortIndexVertices);
        *out++ = quint16(index);
    }
    return packed;
}

} // namespace QcIndexPacking
