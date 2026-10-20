// Payloads of the machine messages (QcMachineMessages.h): furnace, chest and stonecutter screens.

#include "common/protocol/QcProtocolCodec.h"

#include "common/content/registry/QcStonecutting.h"
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
    const std::optional<quint32> countRead = QcStreamLimits::readCount(stream, QcMsg::MachineState::maxSlots);
    if (!countRead || *countRead == 0) {
        return qcError(u"Corrupt machine state"_s);
    }
    const quint32 count = *countRead;

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

void encode(QDataStream &stream, QcMsg::OpenStonecutter m)
{
    writePos(stream, m.pos);
}

void encode(QDataStream &stream, const QcMsg::StonecutterState &m)
{
    writeStack(stream, m.input);
    stream << m.recipe;
}

void encode(QDataStream &stream, const QcMsg::SelectStonecutterRecipe &m)
{
    stream << m.recipe;
}

void encode(QDataStream &, QcMsg::CloseStonecutter) {}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::OpenStonecutter>, QDataStream &stream)
{
    QcMsg::OpenStonecutter m;
    m.pos = readPos(stream);
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::StonecutterState>, QDataStream &stream)
{
    QcMsg::StonecutterState m;
    m.input = readStack(stream);
    stream >> m.recipe;
    if (m.recipe.size() > QcStonecutting::maxRecipeName) {
        return qcError(u"Invalid stonecutting recipe"_s);
    }
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::SelectStonecutterRecipe>, QDataStream &stream)
{
    QcMsg::SelectStonecutterRecipe m;
    stream >> m.recipe;
    if (m.recipe.size() > QcStonecutting::maxRecipeName) {
        return qcError(u"Invalid stonecutting recipe"_s);
    }
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CloseStonecutter>, QDataStream &)
{
    return QcMsg::CloseStonecutter{};
}

} // namespace QcProtocolCodec
