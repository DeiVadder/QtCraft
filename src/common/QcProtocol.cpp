#include "QcProtocol.h"

#include "QcStreamLimits.h"

#include <QDataStream>
#include <QIODevice>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr QDataStream::Version streamVersion = QDataStream::Qt_6_8;

QDataStream &operator<<(QDataStream &stream, QcVec3 value)
{
    return stream << value.x << value.y << value.z;
}

QDataStream &operator>>(QDataStream &stream, QcVec3 &value)
{
    return stream >> value.x >> value.y >> value.z;
}

void writeSection(QDataStream &stream, const QcChunkSection &section)
{
    stream << quint32(section.palette().size());
    for (const QcBlockId id : section.palette()) {
        stream << id;
    }
    if (section.isUniform()) {
        return;
    }
    // Run-length encoded palette indices: terrain sections consist mostly of long runs. Decoding
    // is bounded by the section size, unlike a general-purpose decompressor.
    const QList<quint16> &indices = section.indices();
    QList<std::pair<quint16, quint16>> runs;
    for (qsizetype i = 0; i < indices.size();) {
        qsizetype end = i + 1;
        while (end < indices.size() && indices[end] == indices[i] && end - i < 0xFFFF) {
            ++end;
        }
        runs.append({quint16(end - i), indices[i]});
        i = end;
    }
    stream << quint32(runs.size());
    for (const auto &[length, index] : std::as_const(runs)) {
        stream << length << index;
    }
}

bool readSection(QDataStream &stream, QcChunkSection &section)
{
    quint32 paletteSize = 0;
    if (!QcStreamLimits::readCount(stream, QcWorld::blocksPerSection, paletteSize) || paletteSize == 0) {
        return false;
    }
    QList<QcBlockId> palette(paletteSize);
    for (QcBlockId &id : palette) {
        stream >> id;
    }
    QList<quint16> indices;
    if (paletteSize > 1) {
        quint32 runCount = 0;
        if (!QcStreamLimits::readCount(stream, QcWorld::blocksPerSection, runCount)) {
            return false;
        }
        indices.reserve(QcWorld::blocksPerSection);
        for (quint32 run = 0; run < runCount; ++run) {
            quint16 length = 0;
            quint16 index = 0;
            stream >> length >> index;
            if (stream.status() != QDataStream::Ok || length == 0
                || indices.size() + length > QcWorld::blocksPerSection) {
                return false;
            }
            indices.insert(indices.size(), length, index);
        }
    }
    return stream.status() == QDataStream::Ok && section.assign(std::move(palette), std::move(indices));
}

struct Encoder
{
    QDataStream &stream;

    void operator()(const QcMsg::Hello &m) { stream << m.protocolVersion << m.playerName; }
    void operator()(const QcMsg::Welcome &m) { stream << m.protocolVersion << m.playerEntityId << m.worldName << m.seed; }
    void operator()(const QcMsg::ContentSync &m)
    {
        Q_ASSERT(m.content);
        m.content->writeTo(stream);
    }
    void operator()(const QcMsg::ChunkData &m)
    {
        Q_ASSERT(m.chunk);
        QcProtocol::writeChunk(stream, *m.chunk);
    }
    void operator()(QcMsg::ChunkUnload m) { stream << qint32(m.pos.x) << qint32(m.pos.z); }
    void operator()(QcMsg::BlockUpdate m) { stream << qint32(m.pos.x) << qint32(m.pos.y) << qint32(m.pos.z) << m.block; }
    void operator()(const QcMsg::PlayerInput &m)
    {
        const QcPlayerInput &in = m.input;
        stream << in.sequence << in.forward << in.strafe << in.jump << in.sneak << in.sprint << in.yaw << in.pitch;
    }
    void operator()(const QcMsg::PlayerState &m)
    {
        stream << m.lastInputSequence << m.motion.position << m.motion.velocity << m.motion.onGround << m.motion.inWater;
    }
    void operator()(const QcMsg::Disconnect &m) { stream << m.reason; }
};

template<typename T>
QcResult<QcMessage> decodePayload(QDataStream &stream);

template<>
QcResult<QcMessage> decodePayload<QcMsg::Hello>(QDataStream &stream)
{
    QcMsg::Hello m;
    stream >> m.protocolVersion >> m.playerName;
    return m;
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::Welcome>(QDataStream &stream)
{
    QcMsg::Welcome m;
    stream >> m.protocolVersion >> m.playerEntityId >> m.worldName >> m.seed;
    return m;
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::ContentSync>(QDataStream &stream)
{
    QcResult<QcContentRegistry> content = QcContentRegistry::readFrom(stream);
    if (!content) {
        return qcError(content.error());
    }
    return QcMsg::ContentSync{std::make_shared<const QcContentRegistry>(std::move(*content))};
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::ChunkData>(QDataStream &stream)
{
    QcResult<std::shared_ptr<QcChunk>> chunk = QcProtocol::readChunk(stream);
    if (!chunk) {
        return qcError(chunk.error());
    }
    return QcMsg::ChunkData{std::move(*chunk)};
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::ChunkUnload>(QDataStream &stream)
{
    qint32 x = 0;
    qint32 z = 0;
    stream >> x >> z;
    return QcMsg::ChunkUnload{{x, z}};
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::BlockUpdate>(QDataStream &stream)
{
    qint32 x = 0;
    qint32 y = 0;
    qint32 z = 0;
    QcBlockId block = 0;
    stream >> x >> y >> z >> block;
    return QcMsg::BlockUpdate{{x, y, z}, block};
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::PlayerInput>(QDataStream &stream)
{
    QcPlayerInput in;
    stream >> in.sequence >> in.forward >> in.strafe >> in.jump >> in.sneak >> in.sprint >> in.yaw >> in.pitch;
    if (!std::isfinite(in.forward) || !std::isfinite(in.strafe) || !std::isfinite(in.yaw) || !std::isfinite(in.pitch)) {
        return qcError(u"Non-finite player input"_s);
    }
    return QcMsg::PlayerInput{in};
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::PlayerState>(QDataStream &stream)
{
    QcMsg::PlayerState m;
    stream >> m.lastInputSequence >> m.motion.position >> m.motion.velocity >> m.motion.onGround >> m.motion.inWater;
    const auto finite = [](const QcVec3 &v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    if (!finite(m.motion.position) || !finite(m.motion.velocity)) {
        return qcError(u"Non-finite player state"_s);
    }
    return m;
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::Disconnect>(QDataStream &stream)
{
    QcMsg::Disconnect m;
    stream >> m.reason;
    return m;
}

template<std::size_t... I>
QcResult<QcMessage> decodeByTag(quint16 tag, QDataStream &stream, std::index_sequence<I...>)
{
    QcResult<QcMessage> result = qcError(u"Unknown message type %1"_s.arg(tag));
    const auto tryAlternative = [&]<std::size_t Index>() {
        if (tag != Index) {
            return false;
        }
        result = decodePayload<std::variant_alternative_t<Index, QcMessage>>(stream);
        return true;
    };
    (void)(tryAlternative.template operator()<I>() || ...);
    return result;
}

} // namespace

namespace QcProtocol {

QByteArray encode(const QcMessage &message)
{
    QByteArray frame;
    QDataStream stream(&frame, QIODevice::WriteOnly);
    stream.setVersion(streamVersion);
    stream << quint16(message.index());
    std::visit(Encoder{stream}, message);
    return frame;
}

bool isSentBy(std::size_t messageIndex, Peer sender)
{
    const auto is = [messageIndex]<typename T>() {
        return messageIndex == QcMessage(T{}).index();
    };
    if (is.template operator()<QcMsg::Disconnect>()) {
        return true;
    }
    const bool clientMessage = is.template operator()<QcMsg::Hello>() || is.template operator()<QcMsg::PlayerInput>();
    return clientMessage == (sender == Peer::Client);
}

QcResult<QcMessage> decode(const QByteArray &frame, Peer sender)
{
    if (frame.size() > maxFrameBytes) {
        return qcError(u"Frame too large"_s);
    }
    QDataStream stream(frame);
    stream.setVersion(streamVersion);
    quint16 tag = 0;
    stream >> tag;
    if (stream.status() != QDataStream::Ok) {
        return qcError(u"Empty frame"_s);
    }
    if (tag >= std::variant_size_v<QcMessage>) {
        return qcError(u"Unknown message type %1"_s.arg(tag));
    }
    if (!isSentBy(tag, sender)) {
        return qcError(u"Message type %1 is not allowed from this peer"_s.arg(tag));
    }
    QcResult<QcMessage> message = decodeByTag(tag, stream, std::make_index_sequence<std::variant_size_v<QcMessage>>());
    if (message && stream.status() != QDataStream::Ok) {
        return qcError(u"Truncated message of type %1"_s.arg(tag));
    }
    if (message && !stream.atEnd()) {
        return qcError(u"Trailing bytes in message of type %1"_s.arg(tag));
    }
    return message;
}

void writeChunk(QDataStream &stream, const QcChunk &chunk)
{
    stream << qint32(chunk.pos().x) << qint32(chunk.pos().z);
    for (int i = 0; i < QcWorld::sectionCount; ++i) {
        writeSection(stream, chunk.section(i));
    }
}

QcResult<std::shared_ptr<QcChunk>> readChunk(QDataStream &stream)
{
    qint32 x = 0;
    qint32 z = 0;
    stream >> x >> z;
    auto chunk = std::make_shared<QcChunk>(QcChunkPos{x, z});
    for (int i = 0; i < QcWorld::sectionCount; ++i) {
        if (!readSection(stream, chunk->section(i))) {
            return qcError(u"Corrupt chunk data in section %1"_s.arg(i));
        }
    }
    return chunk;
}

} // namespace QcProtocol
