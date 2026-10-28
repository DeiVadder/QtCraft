#pragma once

#include "server/storage/QcEntityRecord.h"
#include "server/storage/QcLoadWarnings.h"

#include <QDataStream>

#include <optional>

class QcContentRegistry;

// The boats of one chunk in its entity blob (decision E-920): after the item frames and paintings
// (QcHangingCodec), so blobs written before boats read unchanged. Per entry the name of its item,
// its place in the table (QcEntityRecord::Ordered) and its components (QcComponentFrames,
// QcEntityCodec::Component::Boat: feet, velocity, yaw).
namespace QcBoatCodec {

void write(QDataStream &stream, const QList<QcEntityRecord::Ordered<QcBoat>> &boats);

// The entries of the record of `chunk` that follow in `stream`, none at its end. A boat of an unknown
// item (or of one that no longer places a boat) is dropped with a warning; nothing for damaged data
// (a boat outside the chunk, values that cannot be).
[[nodiscard]] std::optional<QList<QcEntityRecord::Ordered<QcBoat>>> read(QDataStream &stream, QcChunkPos chunk, const QcContentRegistry &content,
                                                                        QcLoadWarnings &warnings);

} // namespace QcBoatCodec
