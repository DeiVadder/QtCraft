// Payloads of the world messages (QcWorldMessages.h): chunks, block changes, mining, using items on
// blocks, sounds and the weather.

#include "common/protocol/QcProtocolCodec.h"

#include "common/protocol/QcProtocol.h"
#include "common/protocol/QcStreamLimits.h"

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

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ChunkData>, QDataStream &stream)
{
    QcResult<std::shared_ptr<QcChunk>> chunk = QcProtocol::readChunk(stream);
    if (!chunk) {
        return qcError(chunk.error());
    }
    return QcMsg::ChunkData{std::move(*chunk)};
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ChunkUnload>, QDataStream &stream)
{
    qint32 x = 0;
    qint32 z = 0;
    stream >> x >> z;
    return QcMsg::ChunkUnload{{x, z}};
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::BlockUpdate>, QDataStream &stream)
{
    QcMsg::BlockUpdate m;
    m.pos = readPos(stream);
    stream >> m.block;
    return m;
}

void encode(QDataStream &stream, QcMsg::SpawnerDisplay m)
{
    writePos(stream, m.pos);
    stream << m.mob;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::SpawnerDisplay>, QDataStream &stream)
{
    QcMsg::SpawnerDisplay m;
    m.pos = readPos(stream);
    stream >> m.mob;
    if (m.mob < -1) {
        return qcError(u"Invalid spawner display"_s);
    }
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::StartMining>, QDataStream &stream)
{
    return QcMsg::StartMining{readPos(stream)};
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CancelMining>, QDataStream &)
{
    return QcMsg::CancelMining{};
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::FinishMining>, QDataStream &stream)
{
    return QcMsg::FinishMining{readPos(stream)};
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::UseItem>, QDataStream &stream)
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

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::SoundEvent>, QDataStream &stream)
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

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ChunkLabels>, QDataStream &stream)
{
    QcMsg::ChunkLabels m;
    qint32 x = 0;
    qint32 z = 0;
    stream >> x >> z;
    m.pos = {x, z};
    const std::optional<quint32> countRead = QcStreamLimits::readCount(stream, quint32(QcWorldLabel::maxPerChunk));
    if (!countRead) {
        return qcError(u"Too many labels"_s);
    }
    const quint32 count = *countRead;

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

void encode(QDataStream &stream, QcMsg::Weather m)
{
    stream << m.rain << m.thunder << m.climate.dry << qint16(m.climate.snowFrom);
}

void encode(QDataStream &stream, const QcMsg::Lightning &m)
{
    stream << m.position;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::Weather>, QDataStream &stream)
{
    QcMsg::Weather m;
    qint16 snowFrom = 0;
    stream >> m.rain >> m.thunder >> m.climate.dry >> snowFrom;
    m.climate.snowFrom = snowFrom;
    // Written as negated range checks so that NaN fails them too.
    if (!(m.rain >= 0.0f && m.rain <= 1.0f) || !(m.thunder >= 0.0f && m.thunder <= 1.0f)) {
        return qcError(u"Invalid weather"_s);
    }
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::Lightning>, QDataStream &stream)
{
    QcMsg::Lightning m;
    stream >> m.position;
    if (!isFinite(m.position)) {
        return qcError(u"Invalid lightning"_s);
    }
    return m;
}

void encode(QDataStream &stream, const QcMsg::BlockBatch &m)
{
    stream << quint32(m.updates.size());
    for (const QcMsg::BlockUpdate &update : m.updates) {
        encode(stream, update);
    }
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::BlockBatch>, QDataStream &stream)
{
    QcMsg::BlockBatch m;
    const std::optional<quint32> countRead = QcStreamLimits::readCount(stream, QcMsg::BlockBatch::maxUpdates);
    if (!countRead) {
        return qcError(u"Invalid block batch"_s);
    }
    const quint32 count = *countRead;
    m.updates.reserve(count);
    for (quint32 index = 0; index < count && stream.status() == QDataStream::Ok; ++index) {
        QcMsg::BlockUpdate update;
        update.pos = readPos(stream);
        stream >> update.block;
        m.updates.append(update);
    }
    return m;
}

void encode(QDataStream &stream, const QcMsg::Explosion &m)
{
    stream << m.centre << m.power;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::Explosion>, QDataStream &stream)
{
    QcMsg::Explosion m;
    stream >> m.centre >> m.power;
    if (!isFinite(m.centre) || !(m.power > 0.0f && m.power <= 64.0f)) {
        return qcError(u"Invalid explosion"_s);
    }
    return m;
}

void encode(QDataStream &stream, const QcMsg::CampfireDisplay &m)
{
    writePos(stream, m.pos);
    stream << quint32(m.items.size());
    for (const quint32 item : m.items) {
        stream << item;
    }
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CampfireDisplay>, QDataStream &stream)
{
    QcMsg::CampfireDisplay m;
    m.pos = readPos(stream);
    quint32 count = 0;
    stream >> count;
    if (count > quint32(QcMsg::CampfireDisplay::maxItems)) {
        return qcError(u"Invalid campfire display"_s);
    }
    for (quint32 i = 0; i < count; ++i) {
        quint32 item = 0;
        stream >> item;
        m.items.append(item);
    }
    return m;
}

} // namespace QcProtocolCodec
