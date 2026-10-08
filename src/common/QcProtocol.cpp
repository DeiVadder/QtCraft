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

QDataStream &operator<<(QDataStream &stream, const QcAppearance &value)
{
    return stream << value.outfit << value.hair << value.skinColor << value.hairColor << value.shirtColor << value.pantsColor;
}

// Names longer than the limit fail the message; colours keep only their RGB bits.
bool readAppearance(QDataStream &stream, QcAppearance &value)
{
    stream >> value.outfit >> value.hair >> value.skinColor >> value.hairColor >> value.shirtColor >> value.pantsColor;
    for (quint32 *color : {&value.skinColor, &value.hairColor, &value.shirtColor, &value.pantsColor}) {
        *color &= 0xFFFFFFu;
    }
    return value.outfit.size() <= QcAppearance::maxNameLength && value.hair.size() <= QcAppearance::maxNameLength;
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

    void operator()(const QcMsg::Hello &m) { stream << m.protocolVersion << m.playerName << m.appearance; }
    void operator()(const QcMsg::Welcome &m)
    {
        stream << m.protocolVersion << m.playerEntityId << m.worldName << m.seed << m.yaw << m.pitch << m.restoredView;
    }
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
    void operator()(QcMsg::StartMining m) { writePos(m.pos); }
    void operator()(QcMsg::CancelMining) {}
    void operator()(QcMsg::FinishMining m) { writePos(m.pos); }
    void operator()(QcMsg::UseItem m)
    {
        writePos(m.target);
        stream << quint8(m.face);
    }
    void operator()(QcMsg::SelectHotbarSlot m) { stream << m.slot; }
    void operator()(QcMsg::MoveInventoryItem m) { stream << m.from << m.to << m.count; }
    void operator()(const QcMsg::ChatInput &m) { stream << m.text; }
    void writeStacks(const QList<QcItemStack> &stacks)
    {
        stream << quint32(stacks.size());
        for (const QcItemStack &slot : stacks) {
            writeStack(slot);
        }
    }
    void writeStack(QcItemStack slot) { stream << slot.item << qint32(slot.count) << qint32(slot.damage); }
    void operator()(const QcMsg::InventoryState &m)
    {
        writeStacks(m.stacks);
        stream << m.selected;
        writeStacks(m.craftingGrid);
        stream << m.craftingSize;
        writeStack(m.craftingResult);
    }
    void operator()(QcMsg::TakeCraftingResult) {}
    void operator()(const QcMsg::CraftRecipe &m) { stream << m.recipe << m.count; }
    void operator()(QcMsg::CloseCrafting) {}
    void operator()(QcMsg::ConsumeItem) {}
    void operator()(QcMsg::OpenCrafting m) { stream << m.size; }
    void operator()(const QcMsg::ChatMessage &m) { stream << m.text; }
    void operator()(const QcMsg::EntitySpawn &m)
    {
        stream << m.id << m.mobType << m.name << m.position << m.yaw << m.health;
        writeStack(m.item);
        stream << m.appearance << m.pitch << m.flags << m.held;
    }
    void operator()(const QcMsg::EntityMoves &m)
    {
        stream << quint32(m.moves.size());
        for (const QcMsg::EntityMove &move : m.moves) {
            stream << move.id << move.position << move.yaw << move.health << move.hurt << move.pitch << move.flags << move.held;
        }
    }
    void operator()(QcMsg::EntityRemove m) { stream << m.id << m.died; }
    void operator()(const QcMsg::PlayerHealth &m) { stream << m.health << m.maxHealth << m.dead; }
    void operator()(QcMsg::WorldTime m) { stream << m.time << m.dayLength; }
    void operator()(QcMsg::AttackEntity m) { stream << m.id; }
    void operator()(QcMsg::Respawn) {}
    void operator()(QcMsg::DropItem m) { stream << m.wholeStack; }
    void operator()(const QcMsg::SetAppearance &m) { stream << m.appearance; }

    void writePos(QcBlockPos pos) { stream << qint32(pos.x) << qint32(pos.y) << qint32(pos.z); }
};

template<typename T>
QcResult<QcMessage> decodePayload(QDataStream &stream);

template<>
QcResult<QcMessage> decodePayload<QcMsg::Hello>(QDataStream &stream)
{
    QcMsg::Hello m;
    stream >> m.protocolVersion >> m.playerName;
    // Other versions may lay out the rest differently: skip it so the server can report the version
    // mismatch instead of a decode error.
    if (m.protocolVersion != QcProtocol::version) {
        stream.skipRawData(int(stream.device()->bytesAvailable()));
        return m;
    }
    if (!readAppearance(stream, m.appearance)) {
        return qcError(u"Invalid appearance"_s);
    }
    return m;
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::Welcome>(QDataStream &stream)
{
    QcMsg::Welcome m;
    stream >> m.protocolVersion >> m.playerEntityId >> m.worldName >> m.seed >> m.yaw >> m.pitch >> m.restoredView;
    if (!std::isfinite(m.yaw) || !std::isfinite(m.pitch)) {
        return qcError(u"Non-finite view direction"_s);
    }
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

QcBlockPos readPos(QDataStream &stream)
{
    qint32 x = 0;
    qint32 y = 0;
    qint32 z = 0;
    stream >> x >> y >> z;
    return {x, y, z};
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::StartMining>(QDataStream &stream)
{
    return QcMsg::StartMining{readPos(stream)};
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::CancelMining>(QDataStream &)
{
    return QcMsg::CancelMining{};
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::FinishMining>(QDataStream &stream)
{
    return QcMsg::FinishMining{readPos(stream)};
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::UseItem>(QDataStream &stream)
{
    QcMsg::UseItem m;
    m.target = readPos(stream);
    quint8 face = 0;
    stream >> face;
    if (face >= qcBlockFaceCount) {
        return qcError(u"Invalid block face"_s);
    }
    m.face = QcBlockFace(face);
    return m;
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::SelectHotbarSlot>(QDataStream &stream)
{
    QcMsg::SelectHotbarSlot m;
    stream >> m.slot;
    return m;
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::MoveInventoryItem>(QDataStream &stream)
{
    QcMsg::MoveInventoryItem m;
    stream >> m.from >> m.to >> m.count;
    return m;
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::ChatInput>(QDataStream &stream)
{
    QcMsg::ChatInput m;
    stream >> m.text;
    return m;
}

QcItemStack readStack(QDataStream &stream)
{
    QcItemStack slot;
    qint32 amount = 0;
    qint32 damage = 0;
    stream >> slot.item >> amount >> damage;
    slot.count = amount;
    slot.damage = damage;
    return slot;
}

bool readStacks(QDataStream &stream, QList<QcItemStack> &stacks)
{
    quint32 count = 0;
    if (!QcStreamLimits::readCount(stream, 1024, count)) {
        return false;
    }
    for (quint32 i = 0; i < count; ++i) {
        stacks.append(readStack(stream));
    }
    return true;
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::InventoryState>(QDataStream &stream)
{
    QcMsg::InventoryState m;
    if (!readStacks(stream, m.stacks)) {
        return qcError(u"Corrupt inventory"_s);
    }
    stream >> m.selected;
    if (!readStacks(stream, m.craftingGrid)) {
        return qcError(u"Corrupt crafting grid"_s);
    }
    stream >> m.craftingSize;
    m.craftingResult = readStack(stream);
    return m;
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::TakeCraftingResult>(QDataStream &)
{
    return QcMsg::TakeCraftingResult{};
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::CraftRecipe>(QDataStream &stream)
{
    QcMsg::CraftRecipe m;
    stream >> m.recipe >> m.count;
    return m;
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::CloseCrafting>(QDataStream &)
{
    return QcMsg::CloseCrafting{};
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::ConsumeItem>(QDataStream &)
{
    return QcMsg::ConsumeItem{};
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::OpenCrafting>(QDataStream &stream)
{
    QcMsg::OpenCrafting m;
    stream >> m.size;
    return m;
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::ChatMessage>(QDataStream &stream)
{
    QcMsg::ChatMessage m;
    stream >> m.text;
    return m;
}

bool isFinite(const QcVec3 &v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::EntitySpawn>(QDataStream &stream)
{
    QcMsg::EntitySpawn m;
    stream >> m.id >> m.mobType >> m.name >> m.position >> m.yaw >> m.health;
    m.item = readStack(stream);
    if (!readAppearance(stream, m.appearance)) {
        return qcError(u"Invalid appearance"_s);
    }
    stream >> m.pitch >> m.flags >> m.held;
    if (!std::isfinite(m.pitch)) {
        return qcError(u"Non-finite entity data"_s);
    }
    if (!isFinite(m.position) || !std::isfinite(m.yaw) || !std::isfinite(m.health)) {
        return qcError(u"Non-finite entity data"_s);
    }
    return m;
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::EntityMoves>(QDataStream &stream)
{
    QcMsg::EntityMoves m;
    quint32 count = 0;
    if (!QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions, count)) {
        return qcError(u"Corrupt entity moves"_s);
    }
    for (quint32 i = 0; i < count; ++i) {
        QcMsg::EntityMove move;
        stream >> move.id >> move.position >> move.yaw >> move.health >> move.hurt >> move.pitch >> move.flags >> move.held;
        if (!isFinite(move.position) || !std::isfinite(move.yaw) || !std::isfinite(move.health) || !std::isfinite(move.pitch)) {
            return qcError(u"Non-finite entity data"_s);
        }
        m.moves.append(move);
    }
    return m;
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::EntityRemove>(QDataStream &stream)
{
    QcMsg::EntityRemove m;
    stream >> m.id >> m.died;
    return m;
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::PlayerHealth>(QDataStream &stream)
{
    QcMsg::PlayerHealth m;
    stream >> m.health >> m.maxHealth >> m.dead;
    if (!std::isfinite(m.health) || !std::isfinite(m.maxHealth)) {
        return qcError(u"Non-finite health"_s);
    }
    return m;
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::WorldTime>(QDataStream &stream)
{
    QcMsg::WorldTime m;
    stream >> m.time >> m.dayLength;
    if (!std::isfinite(m.time) || !std::isfinite(m.dayLength) || m.dayLength <= 0.0) {
        return qcError(u"Invalid world time"_s);
    }
    return m;
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::AttackEntity>(QDataStream &stream)
{
    QcMsg::AttackEntity m;
    stream >> m.id;
    return m;
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::Respawn>(QDataStream &)
{
    return QcMsg::Respawn{};
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::SetAppearance>(QDataStream &stream)
{
    QcMsg::SetAppearance m;
    if (!readAppearance(stream, m.appearance)) {
        return qcError(u"Invalid appearance"_s);
    }
    return m;
}

template<>
QcResult<QcMessage> decodePayload<QcMsg::DropItem>(QDataStream &stream)
{
    QcMsg::DropItem m;
    stream >> m.wholeStack;
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
    const bool clientMessage = is.template operator()<QcMsg::Hello>() || is.template operator()<QcMsg::PlayerInput>()
                               || is.template operator()<QcMsg::StartMining>() || is.template operator()<QcMsg::CancelMining>()
                               || is.template operator()<QcMsg::FinishMining>() || is.template operator()<QcMsg::UseItem>()
                               || is.template operator()<QcMsg::SelectHotbarSlot>()
                               || is.template operator()<QcMsg::MoveInventoryItem>() || is.template operator()<QcMsg::ChatInput>()
                               || is.template operator()<QcMsg::AttackEntity>() || is.template operator()<QcMsg::Respawn>()
                               || is.template operator()<QcMsg::TakeCraftingResult>() || is.template operator()<QcMsg::CraftRecipe>()
                               || is.template operator()<QcMsg::CloseCrafting>() || is.template operator()<QcMsg::ConsumeItem>()
                               || is.template operator()<QcMsg::DropItem>() || is.template operator()<QcMsg::SetAppearance>();
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
