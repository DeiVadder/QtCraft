// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/storage/QcComponentFrames.h"

#include <QBuffer>
#include <QIODevice>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <utility>

namespace QcComponentFrames {

namespace {

// Overwrites the placeholder at byte `at` of the stream with `value`.
template<typename Value>
void patch(QDataStream &stream, qint64 at, Value value)
{
    // Into the bytes of the buffer the stream writes to, without seeking it back and forth.
    if (auto *buffer = qobject_cast<QBuffer *>(stream.device())) {
        if (stream.byteOrder() == QDataStream::BigEndian) {
            qToBigEndian(value, buffer->buffer().data() + at);
        } else {
            qToLittleEndian(value, buffer->buffer().data() + at);
        }
        return;
    }
    QIODevice &device = *stream.device();
    const qint64 end = device.pos();
    device.seek(at);
    stream << value;
    device.seek(end);
}

} // namespace

void patchLength(QDataStream &stream, qint64 at, quint32 length)
{
    patch(stream, at, length);
}

Writer::Writer(QDataStream &stream, const QcEntityComponents &kept)
    : m_stream(stream)
    , m_kept(kept)
    , m_countAt(stream.device()->pos())
{
    m_stream << quint16{0};
}

const QcEntityComponent *Writer::tailOf(quint16 id, quint16 version) const
{
    const auto tail = std::ranges::find_if(m_kept, [id](const QcEntityComponent &kept) { return kept.tail && kept.id == id; });
    return tail != m_kept.end() && tail->version > version ? &*tail : nullptr;
}

qint64 Writer::begin(quint16 id, quint16 version)
{
    ++m_count;
    m_stream << id << version << quint32{0};
    return m_stream.device()->pos();
}

void Writer::end(qint64 start)
{
    patchLength(m_stream, start - static_cast<qint64>(sizeof(quint32)), static_cast<quint32>(m_stream.device()->pos() - start));
}

void Writer::finish()
{
    for (const QcEntityComponent &kept : m_kept) {
        if (!kept.tail) {
            ++m_count;
            m_stream << kept.id << kept.version << static_cast<quint32>(kept.payload.size());
            m_stream.writeRawData(kept.payload.constData(), static_cast<int>(kept.payload.size()));
        }
    }
    Q_ASSERT(m_count <= maxComponents);

    patch(m_stream, m_countAt, m_count);
}

std::optional<QByteArray> readRaw(QDataStream &stream, qint64 length)
{
    if (length < 0 || length > stream.device()->bytesAvailable()) {
        return std::nullopt;
    }
    QByteArray bytes(static_cast<qsizetype>(length), Qt::Uninitialized);
    if (stream.readRawData(bytes.data(), static_cast<int>(length)) != static_cast<int>(length)) {
        return std::nullopt;
    }
    return bytes;
}

namespace {

// Reads the known component `known` of `version` and `length` in place. Outside: none for damaged
// data; inside: the component with its newer fields to keep, none when there are none.
[[nodiscard]] std::optional<std::optional<QcEntityComponent>> readKnown(QDataStream &stream, const Known &known, quint16 version,
                                                                        quint32 length)
{
    const qint64 start = stream.device()->pos();
    if (!known.read(stream, version, length) || stream.status() != QDataStream::Ok) {
        return std::nullopt;
    }
    const qint64 used = stream.device()->pos() - start;
    if (std::cmp_greater(used, length) || (version <= known.version && std::cmp_not_equal(used, length))) {
        return std::nullopt;
    }
    if (version <= known.version) {
        return std::optional<std::optional<QcEntityComponent>>(std::in_place, std::nullopt);
    }
    std::optional<QByteArray> payload = readRaw(stream, static_cast<qint64>(length) - used);
    if (!payload) {
        return std::nullopt;
    }
    return std::optional<std::optional<QcEntityComponent>>(std::in_place, QcEntityComponent{known.id, version, true, std::move(*payload)});
}

} // namespace

std::optional<ReadComponents> read(QDataStream &stream, std::initializer_list<Known> known, quint32 lengthLimit)
{
    quint16 count = 0;
    stream >> count;
    if (stream.status() != QDataStream::Ok || count > maxComponents) {
        return std::nullopt;
    }
    ReadComponents result;
    std::array<quint16, maxComponents> seen{};
    qsizetype unknown = 0;
    for (quint16 i = 0; i < count; ++i) {
        quint16 id = 0;
        quint16 version = 0;
        quint32 length = 0;
        stream >> id >> version >> length;
        // The length is checked against the data left before anything is read.
        if (stream.status() != QDataStream::Ok || version == 0 || length > lengthLimit || static_cast<qint64>(length) > stream.device()->bytesAvailable()
            || std::find(seen.begin(), seen.begin() + i, id) != seen.begin() + i) {
            return std::nullopt;
        }
        seen[i] = id;

        const auto it = std::ranges::find(known, id, &Known::id);
        if (it != known.end()) {
            std::optional<std::optional<QcEntityComponent>> newer = readKnown(stream, *it, version, length);
            if (!newer) {
                return std::nullopt;
            }
            if (*newer) {
                result.kept.append(std::move(**newer));
            }
            continue;
        }
        std::optional<QByteArray> payload = readRaw(stream, length);
        if (!payload) {
            return std::nullopt;
        }
        QcEntityComponent component{id, version, false, std::move(*payload)};
        if (std::cmp_less(unknown, maxKept)) {
            ++unknown;
            result.kept.append(std::move(component));
        } else {
            ++result.dropped;
        }
    }
    return result;
}

std::optional<QString> readText(QDataStream &stream, qsizetype maxChars)
{
    constexpr quint32 null = 0xffffffffu;
    quint32 bytes = 0;
    stream >> bytes;
    if (stream.status() != QDataStream::Ok) {
        return std::nullopt;
    }
    if (bytes == null) {
        return QString();
    }
    if (bytes % 2 != 0 || bytes > static_cast<quint32>(maxChars) * 2) {
        stream.setStatus(QDataStream::ReadCorruptData);
        return std::nullopt;
    }

    QByteArray raw(static_cast<qsizetype>(bytes), Qt::Uninitialized);
    if (stream.readRawData(raw.data(), static_cast<int>(bytes)) != static_cast<int>(bytes)) {
        stream.setStatus(QDataStream::ReadPastEnd);
        return std::nullopt;
    }
    QString text(static_cast<qsizetype>(bytes / 2), Qt::Uninitialized);
    if (stream.byteOrder() == QDataStream::BigEndian) {
        qFromBigEndian<char16_t>(raw.constData(), text.size(), text.data());
    } else {
        qFromLittleEndian<char16_t>(raw.constData(), text.size(), text.data());
    }
    return text;
}

} // namespace QcComponentFrames
