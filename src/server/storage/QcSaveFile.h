// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcResult.h"
#include "common/gameplay/QcItemStack.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QDataStream>
#include <QString>

/*! \brief The binary save file container shared by every world file except world.json:
    [magic][quint16 version][quint32 payload size][quint32 crc32][payload].

    Readers accept every format version from baseVersion up to their current one. */
namespace QcSaveFile {

/*! The QDataStream version of every payload. */
inline constexpr QDataStream::Version streamVersion = QDataStream::Qt_6_8;
/*! The first format version of every file. */
inline constexpr quint16 baseVersion = 1;

/*! Writes \a payload with \a magic and format \a version to \a path (replacing the file as a
    whole). Returns an error when it cannot be written. */
[[nodiscard]] QcStatus write(const QString &path, QByteArrayView magic, const QByteArray &payload, quint16 version = baseVersion);
/*! Returns the bytes write() puts into a file for \a magic, \a payload and \a version. */
QByteArray bytes(QByteArrayView magic, const QByteArray &payload, quint16 version = baseVersion);
/*! \brief The payload of a file and its format version (at most the newest one the reader asked
    for). \qcfields */
struct Payload
{
    QByteArray bytes;
    quint16 version = baseVersion;
};
/*! Returns the payload of the file \a path, which must start with \a magic and have a format
    from baseVersion to \a newestVersion and a matching checksum; an error otherwise. */
[[nodiscard]] QcResult<Payload> read(const QString &path, QByteArrayView magic, quint16 newestVersion = baseVersion);
/*! Returns the same for \a data, the bytes of a file (\a path names it in messages), with
    \a magic and \a newestVersion. */
[[nodiscard]] QcResult<Payload> parse(const QByteArray &data, const QString &path, QByteArrayView magic, quint16 newestVersion = baseVersion);
/*! Writes \a stack to \a stream by item name (from \a content), count and damage, then its data
    (QcItemDataCodec); empty stacks and invalid items as an empty name. */
void writeStack(QDataStream &stream, const QcItemStack &stack, const QcContentRegistry &content);

/*! Damaged copies kept aside per file at most (<path>.damaged, .damaged.2, …). */
inline constexpr int maxDamagedCopies = 16;
/*! Copies the damaged file \a path to the first free name <path>.damaged, .damaged.2, …
    (access.json). Returns that name, or empty when copying failed or all names are taken. */
QString keepAside(const QString &path);

} // namespace QcSaveFile
