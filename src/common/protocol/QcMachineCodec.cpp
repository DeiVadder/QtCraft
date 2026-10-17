// Payloads of the machine messages (QcMachineMessages.h): furnace and chest screens.

#include "common/protocol/QcProtocolCodec.h"

#include "common/protocol/QcStreamLimits.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace QcProtocolCodec {

void encode(QDataStream &stream, const QcMsg::OpenMachine &m)
{
    writePos(stream, m.pos);
    stream << m.category << m.entity;
}

void encode(QDataStream &stream, const QcMsg::MachineState &m)
{
    writePos(stream, m.pos);
    stream << m.entity;
    writeStacks(stream, m.stacks);
    stream << m.burn << m.progress;
}

void encode(QDataStream &, QcMsg::CloseMachine) {}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::OpenMachine>, QDataStream &stream)
{
    QcMsg::OpenMachine m;
    m.pos = readPos(stream);
    stream >> m.category >> m.entity;
    if (m.category.size() > 64) {
        return qcError(u"Invalid machine category"_s);
    }
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::MachineState>, QDataStream &stream)
{
    QcMsg::MachineState m;
    m.pos = readPos(stream);
    stream >> m.entity;
    quint32 count = 0;
    if (!QcStreamLimits::readCount(stream, QcMsg::MachineState::maxSlots, count) || count == 0) {
        return qcError(u"Corrupt machine state"_s);
    }

    for (quint32 i = 0; i < count; ++i) {
        m.stacks.append(readStack(stream));
    }

    stream >> m.burn >> m.progress;
    if (!std::isfinite(m.burn) || !std::isfinite(m.progress)) {
        return qcError(u"Non-finite machine state"_s);
    }

    m.burn = std::clamp(m.burn, 0.0f, 1.0f);
    m.progress = std::clamp(m.progress, 0.0f, 1.0f);
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CloseMachine>, QDataStream &)
{
    return QcMsg::CloseMachine{};
}

} // namespace QcProtocolCodec
