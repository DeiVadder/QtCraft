// Payloads of the entity messages (QcEntityMessages.h): mobs, other players, dropped items and
// falling blocks.

#include "QcProtocolCodec.h"

#include "QcStreamLimits.h"

#include <cmath>

using namespace Qt::StringLiterals;

namespace QcProtocolCodec {

void encode(QDataStream &stream, const QcMsg::EntitySpawn &m)
{
    stream << m.id << m.mobType << m.name << m.position << m.yaw << m.health;
    writeStack(stream, m.item);
    stream << m.appearance << m.pitch << m.flags << m.held << m.block;
    writeEquipment(stream, m.equipment);
}

void encode(QDataStream &stream, const QcMsg::EntityMoves &m)
{
    stream << quint32(m.moves.size());
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

QcResult<QcMessage> decode(Tag<QcMsg::EntitySpawn>, QDataStream &stream)
{
    QcMsg::EntitySpawn m;
    stream >> m.id >> m.mobType >> m.name >> m.position >> m.yaw >> m.health;
    m.item = readStack(stream);
    if (!readAppearance(stream, m.appearance)) {
        return qcError(u"Invalid appearance"_s);
    }

    stream >> m.pitch >> m.flags >> m.held >> m.block;
    m.equipment = readEquipment(stream);
    if (!std::isfinite(m.pitch)) {
        return qcError(u"Non-finite entity data"_s);
    }
    if (!isFinite(m.position) || !std::isfinite(m.yaw) || !std::isfinite(m.health)) {
        return qcError(u"Non-finite entity data"_s);
    }
    return m;
}

QcResult<QcMessage> decode(Tag<QcMsg::EntityMoves>, QDataStream &stream)
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

QcResult<QcMessage> decode(Tag<QcMsg::EntityRemove>, QDataStream &stream)
{
    QcMsg::EntityRemove m;
    stream >> m.id >> m.died;
    return m;
}

QcResult<QcMessage> decode(Tag<QcMsg::AttackEntity>, QDataStream &stream)
{
    QcMsg::AttackEntity m;
    stream >> m.id;
    return m;
}

QcResult<QcMessage> decode(Tag<QcMsg::UseEntity>, QDataStream &stream)
{
    QcMsg::UseEntity m;
    stream >> m.id;
    return m;
}

} // namespace QcProtocolCodec
