// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Protocol messages about dimensions (decision E-1002). Part of the QcMessage variant
// (QcMessages.h).

#include "common/content/worldgen/QcDimensionDefinition.h"

namespace QcMsg {

/*! \brief Server -> client: the player is in \c {dimension} from now on (decision E-1002).

    Sent once when he joins, after the content, and every time he moves to another dimension.
    On a move the client forgets the world it knew: chunks, meshes, light, creatures, labels and
    the weather, and shows a loading screen until the new place is meshed; the chunks, creatures,
    weather and position of the new dimension follow as usual.

    Only the client's part of the definition travels: name, translation, ceiling, coordinate
    scale, sky, fog colour, ambient light, sky light, day cycle, weather and whether compasses
    spin. The other fields keep their defaults on the client. */
struct DimensionChange
{
    /*! The dimension, its client's part. */
    QcDimensionDefinition dimension;
};

} // namespace QcMsg
