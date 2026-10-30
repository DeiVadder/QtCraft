// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDataStream>
#include <QStringList>

#include <optional>

/*! \brief Bounds for counts read from untrusted streams (network, save files), so corrupt input
    cannot trigger huge allocations.

    Strings and byte arrays are bounded by the frame size (QcProtocol::maxFrameBytes);
    QDataStream reads them incrementally, so an announced length beyond the frame fails without
    allocating it. */
namespace QcStreamLimits {

/*! The bound of most counts of the content (definitions of one kind, the drops of a block, the
    translations of one file). */
inline constexpr quint32 maxDefinitions = 1u << 16;
/*! Translations of all languages together (decision E-371). A short entry costs about 100 bytes
    of heap but only 16 bytes in a stream, so the frame size alone would let 2 million entries
    (200 MB per copy) through; the base game holds about 2,600. */
inline constexpr quint32 maxTranslations = 1u << 18;

/*! Reads a quint32 count from \a stream and checks it against \a limit. Returns the count, or
    std::nullopt when the stream fails or the count is above the limit (the stream is then set to
    ReadCorruptData). */
[[nodiscard]] inline std::optional<quint32> readCount(QDataStream &stream, quint32 limit)
{
    quint32 count = 0;
    stream >> count;
    if (stream.status() != QDataStream::Ok) {
        return std::nullopt;
    }
    if (count > limit) {
        stream.setStatus(QDataStream::ReadCorruptData);
        return std::nullopt;
    }
    return count;
}

/*! Reads a list written as a QStringList (count, then the strings) from \a stream without
    trusting the count: QDataStream's own QStringList operator reserves whatever count the stream
    announces. Returns the list, or std::nullopt when the stream fails, the count is above
    \a maxCount or a string is longer than \a maxLength (the stream is then set to
    ReadCorruptData). */
[[nodiscard]] inline std::optional<QStringList> readBoundedStrings(QDataStream &stream, quint32 maxCount, qsizetype maxLength)
{
    const std::optional<quint32> count = readCount(stream, maxCount);
    if (!count) {
        return std::nullopt;
    }

    QStringList strings;
    for (quint32 i = 0; i < *count; ++i) {
        QString text;
        stream >> text;
        if (stream.status() != QDataStream::Ok) {
            return std::nullopt;
        }
        if (text.size() > maxLength) {
            stream.setStatus(QDataStream::ReadCorruptData);
            return std::nullopt;
        }
        strings.append(text);
    }
    return strings;
}

} // namespace QcStreamLimits
