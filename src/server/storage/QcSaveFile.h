#pragma once

#include "QcContentRegistry.h"
#include "QcItemStack.h"
#include "QcResult.h"

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

QcStatus write(const QString &path, QByteArrayView magic, const QByteArray &payload, quint16 version = baseVersion);
// `version` receives the file's format version, at most `newestVersion`.
QcResult<QByteArray> read(const QString &path, QByteArrayView magic, quint16 newestVersion = baseVersion, quint16 *version = nullptr);
// An item stack by item name, count and damage; empty stacks and invalid items as an empty name.
void writeStack(QDataStream &stream, QcItemStack stack, const QcContentRegistry &content);

} // namespace QcSaveFile
