// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcWorldConstants.h"
#include "server/storage/QcBlockEntityRecords.h"
#include "server/storage/QcRegionFile.h"

/*! \brief The versions and limits of a chunk's block entity blob (decision E-440), as
    QcBlockEntityCodec writes and reads it. */
namespace QcBlockEntityFormat {

/*! The version every table and every entry without a version of its own writes. \qcshared */
inline constexpr quint16 tableVersion = 1;
inline constexpr quint16 entryVersion = 1;
/*! Signs write version 2: the colour of the text after the lines (decision E-830). Older readers
    keep it as the tail of a newer version. */
inline constexpr quint16 signEntryVersion = 2;
/*! Containers and machines write version 2: their custom name after the fields (decision E-873).
    Older readers keep it as the tail of a newer version; version 1 entries have no name. */
inline constexpr quint16 namedEntryVersion = 2;
/*! Machines write version 3: the slots after the furnace's three (a brewing stand, decision
    E-1063) behind the name. Older readers keep them as the tail of a newer version. */
inline constexpr quint16 machineEntryVersion = 3;
/*! Slots after the furnace's three at most. */
inline constexpr quint8 maxExtraSlots = 8;

/*! Returns the version the entries of \a table are written in. */
constexpr quint16 entryVersionOf(quint16 table)
{
    switch (static_cast<QcBlockEntityRecord::Table>(table)) {
    case QcBlockEntityRecord::Table::Signs:
        return signEntryVersion;
    case QcBlockEntityRecord::Table::Containers:
        return namedEntryVersion;
    case QcBlockEntityRecord::Table::Machines:
        return machineEntryVersion;
    default:
        return entryVersion;
    }
}

/*! Bytes of one table at most: far more than one component of an entity (a chunk full of chests,
    a circuit's long list of waiting positions), but never more than the blob of the whole chunk
    may hold in its region file (QcBlockEntityCodec::encodeWithin keeps every saved blob within
    it). */
inline constexpr quint32 maxTableLength = QcRegionFile::maxBlobBytes;
/*! Entries of one kind in one chunk at most: one per cell. */
inline constexpr quint32 maxPerChunk = QcWorld::chunkSize * QcWorld::chunkSize * QcWorld::worldHeight;
/*! Kept tails of entries of a newer version per chunk, over all tables; beyond them they are
    dropped with a warning. */
inline constexpr quint32 maxKeptTails = maxPerChunk;
/*! Positions the circuits wait to look at, per chunk (QcServerLimits::signalMaxPendingChanges at
    most). */
inline constexpr quint32 maxWakes = 1u << 24;
/*! The longest time a saved duration may have, in seconds. */
inline constexpr double maxSeconds = 100000.0;
/*! Waits are saved as ticks; longer ones are damaged values and cut to this
    (QcBlockEntityRecords). */
inline constexpr quint32 maxTicks = 1u << 20;
/*! Planting orders count up from 0; one near the top could make the next order wrap. */
inline constexpr quint64 maxOrder = quint64{1} << 62;
/*! Characters of a content name at most (a spawner's kind). */
inline constexpr qsizetype maxNameLength = 256;
/*! UTF-16 units of a saved line of sign text at most (cleaned to
    QcSignDefinition::maxLineLength characters when read). */
inline constexpr qsizetype maxLineUnits = 256;

} // namespace QcBlockEntityFormat
