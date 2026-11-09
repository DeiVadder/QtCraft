// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Protocol messages about dimensions (decision E-1002). Part of the QcMessage variant
// (QcMessages.h).

#include "common/content/worldgen/QcDimensionDefinition.h"
#include "common/world/QcAtmosphere.h"

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

/*! \brief Server -> client: the atmosphere of the biome where the player stands (decision
    E-1120): fog, ambient particles, ambience and music.

    Sent once the player plays, again whenever he walks into a biome with another atmosphere and
    after every move to another dimension; an empty one (overworld biomes) keeps the dimension's
    fog and the usual ambience. The client blends from the old to the new one. */
struct Atmosphere
{
    /*! The atmosphere where the player stands. */
    QcBiomeAtmosphere atmosphere;

    friend bool operator==(const Atmosphere &, const Atmosphere &) = default;
};

} // namespace QcMsg
