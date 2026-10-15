#pragma once

#include "QcChunk.h"
#include "QcContentRegistry.h"
#include "QcResult.h"

#include <QByteArray>
#include <QSet>
#include <QStringList>

#include <memory>

// The blocks of one chunk as a blob of a region file (decision E-373): a palette of block state
// names ("name[property=value,...]") local to the chunk and the sections in the protocol's chunk
// encoding with indices into it, compressed. Content changes between sessions do not scramble a
// world: unknown names become air with a warning.
namespace QcChunkCodec {

QByteArray encode(const QcChunk &chunk, const QcContentRegistry &content);
// `chunk` is where the blob says it belongs; anything else is damage. `reported` holds the names
// warned about so far, so each unknown name is reported once per world.
QcResult<std::shared_ptr<QcChunk>> decode(const QByteArray &blob, QcChunkPos chunk, const QcContentRegistry &content,
                                          QStringList &warnings, QSet<QString> &reported);

} // namespace QcChunkCodec
