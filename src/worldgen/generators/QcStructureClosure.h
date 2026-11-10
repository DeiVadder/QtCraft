// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcChunkSet.h"
#include "common/world/QcConversionControl.h"

#include <optional>

class QcWorldGenerator;

/*! \brief The chunks a generator switch keeps at the old version (decision E-1013): the explored
    chunks and every chunk a structure built into them reaches.

    A village, mine shaft, dungeon, coastal tower or surface structure that an explored chunk
    already started is thus generated whole by the old version, also where it reaches into
    chunks never generated (with the old terrain around it, which it was planned on). Every
    generator of a versioned dimension builds only structures wholly in its own chunks
    (QcVersionedGenerator), so the new version builds nothing that overlaps it. */
namespace QcStructureClosure {

/*! Returns \a explored with all chunks that the structures \a generator builds into its chunks
    reach (QcWorldGenerator::structuresTouching), reporting the chunks surveyed to \a control
    (stage Survey); none when \a control stops it. */
[[nodiscard]] std::optional<QcChunkSet> of(const QcWorldGenerator &generator, const QcChunkSet &explored, const QcConversionControl &control);

} // namespace QcStructureClosure
