// Payloads of the world messages (QcWorldMessages.h): chunks, block changes, mining, using items on
// blocks and sounds.

#include "QcProtocolCodec.h"

#include "QcProtocol.h"
#include "QcStreamLimits.h"

#include <cmath>

using namespace Qt::StringLiterals;

namespace QcProtocolCodec {

void encode(QDataStream &stream, const QcMsg::ChunkData &m)
{
    Q_ASSERT(m.chunk);
    QcProtocol::writeChunk(stream, *m.chunk);
}

void encode(QDataStream &stream, QcMsg::ChunkUnload m)
{
    stream << qint32(m.pos.x) << qint32(m.pos.z);
}

void encode(QDataStream &stream, QcMsg::BlockUpdate m)
{
    writePos(stream, m.pos);
    stream << m.block;
}

void encode(QDataStream &stream, QcMsg::StartMining m)
{
    writePos(stream, m.pos);
}

void encode(QDataStream &, QcMsg::CancelMining) {}

void encode(QDataStream &stream, QcMsg::FinishMining m)
{
    writePos(stream, m.pos);
}

void encode(QDataStream &stream, const QcMsg::UseItem &m)
{
    writePos(stream, m.target);
    stream << quint8(m.face) << m.hit;
}

void encode(QDataStream &stream, const QcMsg::SoundEvent &m)
{
    stream << m.key << m.position << m.volume << m.pitch;
}

void encode(QDataStream &stream, const QcMsg::ChunkLabels &m)
{
    stream << qint32(m.pos.x) << qint32(m.pos.z) << quint32(m.labels.size());
    for (const QcWorldLabel &label : m.labels) {
        stream << label.position << label.text << label.detail << label.heading;
    }
}

QcResult<QcMessage> decode(Tag<QcMsg::ChunkData>, QDataStream &stream)
{
    QcResult<std::shared_ptr<QcChunk>> chunk = QcProtocol::readChunk(stream);
    if (!chunk) {
        return qcError(chunk.error());
    }
    return QcMsg::ChunkData{std::move(*chunk)};
}

QcResult<QcMessage> decode(Tag<QcMsg::ChunkUnload>, QDataStream &stream)
{
    qint32 x = 0;
    qint32 z = 0;
    stream >> x >> z;
    return QcMsg::ChunkUnload{{x, z}};
}

QcResult<QcMessage> decode(Tag<QcMsg::BlockUpdate>, QDataStream &stream)
{
    QcMsg::BlockUpdate m;
    m.pos = readPos(stream);
    stream >> m.block;
    return m;
}

QcResult<QcMessage> decode(Tag<QcMsg::StartMining>, QDataStream &stream)
{
    return QcMsg::StartMining{readPos(stream)};
}

QcResult<QcMessage> decode(Tag<QcMsg::CancelMining>, QDataStream &)
{
    return QcMsg::CancelMining{};
}

QcResult<QcMessage> decode(Tag<QcMsg::FinishMining>, QDataStream &stream)
{
    return QcMsg::FinishMining{readPos(stream)};
}

QcResult<QcMessage> decode(Tag<QcMsg::UseItem>, QDataStream &stream)
{
    QcMsg::UseItem m;
    m.target = readPos(stream);
    quint8 face = 0;
    stream >> face >> m.hit;
    if (face >= qcBlockFaceCount) {
        return qcError(u"Invalid block face"_s);
    }

    const auto inCell = [](double value) { return std::isfinite(value) && value >= 0.0 && value <= 1.0; };
    if (!inCell(m.hit.x) || !inCell(m.hit.y) || !inCell(m.hit.z)) {
        return qcError(u"Invalid hit point"_s);
    }
    m.face = QcBlockFace(face);
    return m;
}

QcResult<QcMessage> decode(Tag<QcMsg::SoundEvent>, QDataStream &stream)
{
    QcMsg::SoundEvent m;
    stream >> m.key >> m.position >> m.volume >> m.pitch;
    if (m.key.isEmpty() || m.key.size() > QcMsg::SoundEvent::maxKeyLength) {
        return qcError(u"Invalid sound name"_s);
    }

    // Written as negated range checks so that NaN fails them too.
    if (!isFinite(m.position) || !(m.volume >= 0.0f && m.volume <= QcMsg::SoundEvent::maxVolume)
        || !(m.pitch >= QcMsg::SoundEvent::minPitch && m.pitch <= QcMsg::SoundEvent::maxPitch)) {
        return qcError(u"Invalid sound event"_s);
    }
    return m;
}

QcResult<QcMessage> decode(Tag<QcMsg::ChunkLabels>, QDataStream &stream)
{
    QcMsg::ChunkLabels m;
    qint32 x = 0;
    qint32 z = 0;
    quint32 count = 0;
    stream >> x >> z;
    m.pos = {x, z};
    if (!QcStreamLimits::readCount(stream, quint32(QcWorldLabel::maxPerChunk), count)) {
        return qcError(u"Too many labels"_s);
    }

    for (quint32 i = 0; i < count; ++i) {
        QcWorldLabel label;
        stream >> label.position >> label.text >> label.detail >> label.heading;
        const bool sized = label.text.size() <= QcWorldLabel::maxTextLength && label.detail.size() <= QcWorldLabel::maxTextLength;
        if (!isFinite(label.position) || !sized) {
            return qcError(u"Invalid label"_s);
        }
        m.labels.append(label);
    }
    return m;
}

} // namespace QcProtocolCodec
