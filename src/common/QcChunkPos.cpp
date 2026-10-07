#include "QcChunkPos.h"

#include <QHashFunctions>

#include <algorithm>
#include <cstdlib>

size_t qHash(QcChunkPos pos, size_t seed) noexcept
{
    return qHashMulti(seed, pos.x, pos.z);
}

int qcChunkDistance(QcChunkPos a, QcChunkPos b)
{
    return std::max(std::abs(a.x - b.x), std::abs(a.z - b.z));
}
