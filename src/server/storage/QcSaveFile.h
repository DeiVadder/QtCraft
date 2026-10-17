#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcResult.h"
#include "common/gameplay/QcItemStack.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QDataStream>
#include <QString>

// The binary save file container shared by every world file except world.json:
// [magic][quint16 version][quint32 payload size][quint32 crc32][payload]. Readers accept every
// format version from baseVersion up to their current one.
namespace QcSaveFile {

inline constexpr QDataStream::Version streamVersion = QDataStream::Qt_6_8;
inline constexpr quint16 baseVersion = 1;

[[nodiscard]] QcStatus write(const QString &path, QByteArrayView magic, const QByteArray &payload, quint16 version = baseVersion);
// The bytes write() puts into a file.
QByteArray bytes(QByteArrayView magic, const QByteArray &payload, quint16 version = baseVersion);
// `version` receives the file's format version, at most `newestVersion`.
[[nodiscard]] QcResult<QByteArray> read(const QString &path, QByteArrayView magic, quint16 newestVersion = baseVersion, quint16 *version = nullptr);
// The same for the bytes of a file (`path` names it in messages).
[[nodiscard]] QcResult<QByteArray> parse(const QByteArray &data, const QString &path, QByteArrayView magic, quint16 newestVersion = baseVersion,
                           quint16 *version = nullptr);
// An item stack by item name, count and damage; empty stacks and invalid items as an empty name.
void writeStack(QDataStream &stream, QcItemStack stack, const QcContentRegistry &content);

// Damaged copies kept aside per file at most (<path>.damaged, .damaged.2, …).
inline constexpr int maxDamagedCopies = 16;
// Copies a damaged file to the first free name <path>.damaged, .damaged.2, … (access.json); that
// name, or empty when copying failed or all names are taken.
QString keepAside(const QString &path);

} // namespace QcSaveFile
