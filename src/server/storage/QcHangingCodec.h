// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/storage/QcEntityRecord.h"
#include "server/storage/QcLoadWarnings.h"

#include <QDataStream>

#include <optional>

class QcContentRegistry;

// The item frames and paintings of one chunk in its entity blob (decision E-793): after the flag
// whether the chunk was populated, so blobs written before them read unchanged. Per entry the name
// of its item, its place in the table (QcEntityRecord::Ordered) and its components
// (QcComponentFrames, QcEntityCodec::Component::Hanging).
namespace QcHangingCodec {

void write(QDataStream &stream, const QList<QcEntityRecord::Ordered<QcHangingEntity>> &hangings, const QcContentRegistry &content);

// The entries of the record of `chunk` that follow in `stream`, none at its end (every entry hangs
// in `chunk`). An unknown item or motif, or a painting whose motif grew out of the chunk, drops
// its entry with a warning; nothing for damaged data (values that cannot be).
[[nodiscard]] std::optional<QList<QcEntityRecord::Ordered<QcHangingEntity>>> read(QDataStream &stream, QcChunkPos chunk, const QcContentRegistry &content,
                                                                               QcLoadWarnings &warnings);

} // namespace QcHangingCodec
