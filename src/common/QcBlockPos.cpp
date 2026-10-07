#include "QcBlockPos.h"

#include <QHashFunctions>

size_t qHash(QcBlockPos pos, size_t seed) noexcept
{
    return qHashMulti(seed, pos.x, pos.y, pos.z);
}
