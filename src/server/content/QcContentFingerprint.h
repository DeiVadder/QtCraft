#pragma once

#include "QcLoginMessages.h"
#include "QcModLoader.h"
#include "QcResult.h"

// What a network server and its clients compare at the login (decision E-252): per loaded content
// pack its id, version and a hash over its files, so that two installations with the same mod
// versions but changed files are told apart.
namespace QcContentFingerprint {

// Packs larger than this are compared by id and version only.
inline constexpr qint64 maxPackBytes = qint64(256) * 1024 * 1024;
inline constexpr int maxPackFiles = 20'000;

// Lower-case hex of the first 16 bytes of SHA-256 over the files the content loader reads from the
// pack in `directory` (also a Qt resource directory; QcContentPackLoader::contentFiles): relative
// path, size and contents, in the order of the sorted paths. Other files (.DS_Store, notes) do not count.
[[nodiscard]] QcResult<QString> hashOf(const QString &directory);
// The packs of `content` in load order: the base game and the loaded mods of `mods` (their
// directories); a pack that cannot be hashed gets an empty hash.
QList<QcPackFingerprint> of(const QcContentRegistry &content, const QList<QcModStatus> &mods);

} // namespace QcContentFingerprint
