#include "server/storage/QcComponentFrames.h"

#include <QBuffer>
#include <QIODevice>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <utility>

namespace QcComponentFrames {

Writer::Writer(QDataStream &stream, const QcEntityComponents &kept)
    : m_stream(stream)
    , m_kept(kept)
    , m_countAt(stream.device()->pos())
{
    m_stream << quint16(0);
}

const QcEntityComponent *Writer::tailOf(quint16 id, quint16 version) const
{
    const auto tail = std::ranges::find_if(m_kept, [id](const QcEntityComponent &kept) { return kept.tail && kept.id == id; });
    return tail != m_kept.end() && tail->version > version ? &*tail : nullptr;
}

qint64 Writer::begin(quint16 id, quint16 version)
{
    ++m_count;
    m_stream << id << version << quint32(0);
    return m_stream.device()->pos();
}

void Writer::end(qint64 start)
{
    patch(start - qint64(sizeof(quint32)), quint32(m_stream.device()->pos() - start));
}

template<typename Value>
void Writer::patch(qint64 at, Value value)
{
    // Into the bytes of the buffer the stream writes to, without seeking it back and forth.
    if (auto *buffer = qobject_cast<QBuffer *>(m_stream.device())) {
        if (m_stream.byteOrder() == QDataStream::BigEndian) {
            qToBigEndian(value, buffer->buffer().data() + at);
        } else {
            qToLittleEndian(value, buffer->buffer().data() + at);
        }
        return;
    }
    QIODevice &device = *m_stream.device();
    const qint64 end = device.pos();
    device.seek(at);
    m_stream << value;
    device.seek(end);
}

void Writer::finish()
{
    for (const QcEntityComponent &kept : m_kept) {
        if (!kept.tail) {
            ++m_count;
            m_stream << kept.id << kept.version << quint32(kept.payload.size());
            m_stream.writeRawData(kept.payload.constData(), int(kept.payload.size()));
        }
    }
    Q_ASSERT(m_count <= maxComponents);

    patch(m_countAt, m_count);
}

namespace {

// The raw bytes of `length` that follow in `stream`, checked against the data left before anything
// is allocated.
bool readRaw(QDataStream &stream, qint64 length, QByteArray &bytes)
{
    if (length > stream.device()->bytesAvailable()) {
        return false;
    }
    bytes.resize(qsizetype(length));
    return stream.readRawData(bytes.data(), int(length)) == int(length);
}

// Reads the known component `known` of `version` and `length` in place; its newer fields go to `kept`.
bool readKnown(QDataStream &stream, const Known &known, quint16 version, quint32 length, QcEntityComponents &kept)
{
    const qint64 start = stream.device()->pos();
    if (!known.read(stream, version) || stream.status() != QDataStream::Ok) {
        return false;
    }
    const qint64 used = stream.device()->pos() - start;
    if (std::cmp_greater(used, length) || (version <= known.version && std::cmp_not_equal(used, length))) {
        return false;
    }
    if (version <= known.version) {
        return true;
    }
    QcEntityComponent tail{known.id, version, true, {}};
    if (!readRaw(stream, qint64(length) - used, tail.payload)) {
        return false;
    }
    kept.append(std::move(tail));
    return true;
}

} // namespace

bool read(QDataStream &stream, std::initializer_list<Known> known, QcEntityComponents &kept, int &dropped)
{
    quint16 count = 0;
    stream >> count;
    if (stream.status() != QDataStream::Ok || count > maxComponents) {
        return false;
    }
    std::array<quint16, maxComponents> seen{};
    qsizetype unknown = 0;
    for (quint16 i = 0; i < count; ++i) {
        quint16 id = 0;
        quint16 version = 0;
        quint32 length = 0;
        stream >> id >> version >> length;
        // The length is checked against the data left before anything is read.
        if (stream.status() != QDataStream::Ok || version == 0 || length > maxLength || qint64(length) > stream.device()->bytesAvailable()
            || std::find(seen.begin(), seen.begin() + i, id) != seen.begin() + i) {
            return false;
        }
        seen[i] = id;

        const auto it = std::ranges::find(known, id, &Known::id);
        if (it != known.end()) {
            if (!readKnown(stream, *it, version, length, kept)) {
                return false;
            }
            continue;
        }
        QcEntityComponent component{id, version, false, {}};
        if (!readRaw(stream, length, component.payload)) {
            return false;
        }
        if (std::cmp_less(unknown, maxKept)) {
            ++unknown;
            kept.append(std::move(component));
        } else {
            ++dropped;
        }
    }
    return true;
}

bool readText(QDataStream &stream, QString &text, qsizetype maxChars)
{
    constexpr quint32 null = 0xffffffffu;
    quint32 bytes = 0;
    stream >> bytes;
    if (stream.status() != QDataStream::Ok) {
        return false;
    }
    if (bytes == null) {
        text = QString();
        return true;
    }
    if (bytes % 2 != 0 || bytes > quint32(maxChars) * 2) {
        stream.setStatus(QDataStream::ReadCorruptData);
        return false;
    }

    QByteArray raw(qsizetype(bytes), Qt::Uninitialized);
    if (stream.readRawData(raw.data(), int(bytes)) != int(bytes)) {
        stream.setStatus(QDataStream::ReadPastEnd);
        return false;
    }
    text.resize(qsizetype(bytes / 2));
    if (stream.byteOrder() == QDataStream::BigEndian) {
        qFromBigEndian<char16_t>(raw.constData(), text.size(), text.data());
    } else {
        qFromLittleEndian<char16_t>(raw.constData(), text.size(), text.data());
    }
    return true;
}

} // namespace QcComponentFrames
