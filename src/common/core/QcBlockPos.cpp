// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/core/QcBlockPos.h"

#include <QHashFunctions>

size_t qHash(QcBlockPos pos, size_t seed) noexcept
{
    return qHashMulti(seed, pos.x, pos.y, pos.z);
}
