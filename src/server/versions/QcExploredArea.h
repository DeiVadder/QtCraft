// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"
#include "common/world/QcChunkSet.h"

#include <QString>
#include <QStringList>

class QcContentRegistry;
struct QcDimensionDefinition;

/*! \brief The area of a dimension that was ever generated (decision E-1011): the chunks a switch
    of the generator version keeps at the older version.

    It is the union of what the world knows on disk: the index of generated chunks (decision
    E-946, worlds since 1.0.0), every chunk with stored blocks, block entities or entities (a
    changed chunk, or one whose creatures were placed, \c {populated}) and, for the overworld,
    every chunk under a revealed pixel of a map (a pixel shows a column the server read). Worlds
    older than the index (0.1.0) are thereby covered as far as they can be: a chunk that was only
    looked at, without changes, with creature spawning off and on no map, is not known. Counting
    too much is harmless (such a chunk is generated with the older version, like its neighbours),
    so a map pixel counts every chunk it covers. */
namespace QcExploredArea {

/*! \brief The explored area and warnings for the player (a damaged map left out). \qcfields */
struct Explored
{
    QcChunkSet chunks;
    QStringList warnings;
};

/*! Returns the explored area of \a dimension in the world directory \a worldDirectory, with the
    blocks of \a content (for the maps). The world must not be open: a save that broke off after
    its journal is finished first, as opening the world would. Returns an error for a damaged
    region file or journal, and for an index of generated chunks that is damaged, was kept
    aside as damaged before (a copy \c {generated.qcg.damaged*} lies there) or has a newer
    format: its chunks cannot be read, so the area would be too small. */
[[nodiscard]] QcResult<Explored> of(const QString &worldDirectory, const QcDimensionDefinition &dimension, const QcContentRegistry &content);

/*! Returns the chunks under the revealed pixels of the maps in the world directory
    \a worldDirectory (maps/), read with the blocks of \a content; a map that cannot be read is
    left out with a warning. */
[[nodiscard]] Explored ofMaps(const QString &worldDirectory, const QcContentRegistry &content);

} // namespace QcExploredArea
