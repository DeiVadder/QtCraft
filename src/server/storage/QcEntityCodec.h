#pragma once

#include "QcContentRegistry.h"
#include "QcEntityRecord.h"
#include "QcResult.h"

#include <QByteArray>
#include <QSet>
#include <QStringList>

// The entities of one chunk as a blob of its region file (entities/r.<x>.<z>.qen; decisions E-366,
// E-373): persistent creatures, dropped items and waiting drops, falling blocks, armour stands,
// minecarts (E-382), experience orbs (E-398), and whether the chunk already got its animals. Items, blocks, creatures, armour
// and capes are stored by name.
namespace QcEntityCodec {

// Entries of one kind in one chunk at most; a valid world never comes close.
inline constexpr quint32 maxPerChunk = 65536;

QByteArray encode(const QcEntityRecord &record, const QcContentRegistry &content);
// The record of `chunk`. Unknown items, blocks, creatures and capes and armour that no longer fits
// its slot are dropped with a warning (each unknown name once per world, through `reported`); damaged
// or implausible data (an entity outside the chunk, values that cannot be) is an error.
[[nodiscard]] QcResult<QcEntityRecord> decode(const QByteArray &blob, QcChunkPos chunk, const QcContentRegistry &content, QStringList &warnings,
                                QSet<QString> &reported);

} // namespace QcEntityCodec
