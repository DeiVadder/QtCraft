// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"

/*! \brief A cube of an effect cloud as the entity frame draws it at a moment (campfire smoke, ink
    clouds; decision E-975): its centre and its edge length. */
struct QcEffectPuff
{
    /*! The centre of the cube. */
    QcVec3 position;
    /*! Its edge length in metres. */
    double size = 0.0;
};
