// Payloads of the entity messages (QcEntityMessages.h).

#include "common/protocol/QcProtocolCodec.h"

#include "common/protocol/QcStreamLimits.h"

#include <cmath>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

void writeHanging(QDataStream &stream, const QcHanging &hanging)
{
    stream << static_cast<quint8>(hanging.type) << static_cast<quint8>(hanging.face) << static_cast<qint32>(hanging.cell.x)
        << static_cast<qint32>(hanging.cell.y) << static_cast<qint32>(hanging.cell.z)
           << static_cast<quint8>(hanging.width) << static_cast<quint8>(hanging.height) << hanging.rotation << hanging.motif;
}

// The hanging part of an EntitySpawn; std::nullopt for values no frame or painting has.
[[nodiscard]] std::optional<QcHanging> readHanging(QDataStream &stream)
{
    quint8 type = 0;
    quint8 face = 0;
    qint32 x = 0;
    qint32 y = 0;
    qint32 z = 0;
    quint8 width = 0;
    quint8 height = 0;
    QcHanging hanging;
    stream >> type >> face >> x >> y >> z >> width >> height >> hanging.rotation >> hanging.motif;
    if (type >= qcHangingTypeCount || face >= qcBlockFaceCount || width < 1 || height < 1 || width > QcPaintingMotif::maxSide
        || height > QcPaintingMotif::maxSide || hanging.rotation >= QcHanging::rotations) {
        return std::nullopt;
    }
    hanging.type = static_cast<QcHangingType>(type);
    hanging.face = static_cast<QcBlockFace>(face);
    hanging.cell = {x, y, z};
    hanging.width = width;
    hanging.height = height;
    return hanging;
}

} // namespace

namespace QcProtocolCodec {

void encode(QDataStream &stream, const QcMsg::EntitySpawn &m)
{
    stream << m.id << static_cast<quint8>(m.kind);
    if (m.kind == QcEntityKind::Creature) {
        stream << m.creatureType;
    }
    stream << m.name << m.position << m.yaw << m.health;
    writeStack(stream, m.item);
    stream << m.appearance << m.pitch << m.flags << m.held << m.block;
    writeEquipment(stream, m.equipment);
    if (m.kind == QcEntityKind::ArmourStand) {
        writeStandPose(stream, m.stand);
    }
    if (m.kind == QcEntityKind::Hanging) {
        writeHanging(stream, m.hanging);
    }
}

void encode(QDataStream &stream, const QcMsg::EntityMoves &m)
{
    stream << m.timeMs << static_cast<quint32>(m.moves.size());
    for (const QcMsg::EntityMove &move : m.moves) {
        stream << move.id << move.position << move.yaw << move.health << move.hurt << move.pitch << move.flags << move.held;
    }
}

void encode(QDataStream &stream, QcMsg::EntityRemove m)
{
    stream << m.id << m.died;
}

void encode(QDataStream &stream, QcMsg::AttackEntity m)
{
    stream << m.id;
}

void encode(QDataStream &stream, QcMsg::UseEntity m)
{
    stream << m.id;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::EntitySpawn>, QDataStream &stream)
{
    QcMsg::EntitySpawn m;
    quint8 kind = 0;
    stream >> m.id >> kind;
    const std::optional<QcEntityKind> known = QcEntityKinds::fromValue(kind);
    if (!known) {
        return qcError(u"Unknown entity kind %1"_s.arg(kind));
    }
    m.kind = *known;
    if (m.kind == QcEntityKind::Creature) {
        stream >> m.creatureType;
        if (m.creatureType < 0 || std::cmp_greater_equal(m.creatureType, QcStreamLimits::maxDefinitions)) {
            return qcError(u"Invalid creature type %1"_s.arg(m.creatureType));
        }
    }

    stream >> m.name >> m.position >> m.yaw >> m.health;
    m.item = readStack(stream);
    std::optional<QcAppearance> appearance = readAppearance(stream);
    if (!appearance) {
        return qcError(u"Invalid appearance"_s);
    }
    m.appearance = std::move(*appearance);

    stream >> m.pitch >> m.flags >> m.held >> m.block;
    m.equipment = readEquipment(stream);
    if (m.kind == QcEntityKind::ArmourStand) {
        m.stand = readStandPose(stream);
    }
    if (m.kind == QcEntityKind::Hanging) {
        const std::optional<QcHanging> hanging = readHanging(stream);
        if (!hanging) {
            return qcError(u"Invalid frame or painting"_s);
        }
        m.hanging = *hanging;
    }
    if (!std::isfinite(m.pitch)) {
        return qcError(u"Non-finite entity data"_s);
    }
    if (!isFinite(m.position) || !std::isfinite(m.yaw) || !std::isfinite(m.health)) {
        return qcError(u"Non-finite entity data"_s);
    }
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::EntityMoves>, QDataStream &stream)
{
    QcMsg::EntityMoves m;
    stream >> m.timeMs;
    const std::optional<quint32> count = m.timeMs < 0 ? std::nullopt : QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions);
    if (!count) {
        return qcError(u"Corrupt entity moves"_s);
    }

    for (quint32 i = 0; i < *count; ++i) {
        QcMsg::EntityMove move;
        stream >> move.id >> move.position >> move.yaw >> move.health >> move.hurt >> move.pitch >> move.flags >> move.held;
        if (!isFinite(move.position) || !std::isfinite(move.yaw) || !std::isfinite(move.health) || !std::isfinite(move.pitch)) {
            return qcError(u"Non-finite entity data"_s);
        }
        m.moves.append(move);
    }
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::EntityRemove>, QDataStream &stream)
{
    QcMsg::EntityRemove m;
    stream >> m.id >> m.died;
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AttackEntity>, QDataStream &stream)
{
    QcMsg::AttackEntity m;
    stream >> m.id;
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::UseEntity>, QDataStream &stream)
{
    QcMsg::UseEntity m;
    stream >> m.id;
    return m;
}

} // namespace QcProtocolCodec
