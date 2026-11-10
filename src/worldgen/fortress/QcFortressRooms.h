// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/fortress/QcFortressCanvas.h"
#include "worldgen/fortress/QcFortressPlan.h"

/*! \brief The insides of the rooms of a fortress (decision E-1072): what stands in a room once its
    shell is built and its air carved out. Each function writes the cells of one chunk. */
namespace QcFortressRooms {

/*! Furnishes \a room on \a canvas after its kind. */
void furnish(QcFortressCanvas &canvas, const QcFortressPlan::Room &room);

} // namespace QcFortressRooms
