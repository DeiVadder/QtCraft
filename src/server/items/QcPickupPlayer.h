// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"

#include <QtTypes>

/*! \brief What a tick of dropped items or experience orbs needs to know of a player: entity id,
    feet position and whether he is alive (decision E-975). \qcfields */
struct QcPickupPlayer
{
    quint32 entityId = 0;
    QcVec3 position;
    bool alive = true;
};
