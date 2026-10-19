#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcResult.h"
#include "common/world/QcChunk.h"
#include "server/storage/QcLoadWarnings.h"

#include <QByteArray>

#include <memory>

// The blocks of one chunk as a blob of a region file (decision E-373): a palette of block state
// names ("name[property=value,...]") local to the chunk and the sections in the protocol's chunk
// encoding with indices into it, compressed. Content changes between sessions do not scramble a
// world: unknown names become air with a warning.
namespace QcChunkCodec {

QByteArray encode(const QcChunk &chunk, const QcContentRegistry &content);
// `chunk` is where the blob says it belongs; anything else is damage. Each unknown name is reported
// once to `warnings` (which reads the whole world).
[[nodiscard]] QcResult<std::shared_ptr<QcChunk>> decode(const QByteArray &blob, QcChunkPos chunk, const QcContentRegistry &content,
                                          QcLoadWarnings &warnings);

} // namespace QcChunkCodec
