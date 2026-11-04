// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"

#include <QString>

/*! \brief Where a player belongs while the dimension he was in cannot open (decision E-989): a mod
    is missing for a while, he plays in the overworld and comes back here when it is there again.
    \qcfields */
struct QcPlayerReturn
{
    QString dimension;
    QcVec3 position;
    float yaw = 0.0f;
    float pitch = 0.0f;

    bool operator==(const QcPlayerReturn &) const = default;
};
