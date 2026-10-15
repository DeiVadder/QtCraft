#pragma once

#include "QcBlockEntityRecord.h"
#include "QcContentRegistry.h"
#include "QcResult.h"

#include <QByteArray>
#include <QSet>
#include <QStringList>

// The block entities of one chunk as a blob of its region file (blockentities/r.<x>.<z>.qcb;
// decisions E-358, E-373): containers and machines with their contents, saplings, what circuit
// components remember, scheduled ticks, positions the circuits wait to look at and pending block
// updates. Items are stored by name.
namespace QcBlockEntityCodec {

QByteArray encode(const QcBlockEntityRecord &record, const QcContentRegistry &content);
// The record of `chunk`. Unknown items are dropped with a warning (once per name in `reported`);
// damaged or implausible data (a position outside the chunk or twice, unknown priorities) is an
// error. Values out of range are cut.
[[nodiscard]] QcResult<QcBlockEntityRecord> decode(const QByteArray &blob, QcChunkPos chunk, const QcContentRegistry &content,
                                     QStringList &warnings, QSet<QString> &reported);

} // namespace QcBlockEntityCodec
