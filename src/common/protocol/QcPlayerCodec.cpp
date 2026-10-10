// Payloads of the player messages (QcPlayerMessages.h): input and state, inventory, crafting,
// health, food and appearance.

#include "QcProtocolCodec.h"

#include <cmath>

using namespace Qt::StringLiterals;

namespace QcProtocolCodec {

void encode(QDataStream &stream, const QcMsg::PlayerInput &m)
{
    const QcPlayerInput &in = m.input;
    stream << in.sequence << in.forward << in.strafe << in.jump << in.sneak << in.sprint << in.yaw << in.pitch;
}

void encode(QDataStream &stream, const QcMsg::PlayerState &m)
{
    stream << m.lastInputSequence << m.motion.position << m.motion.velocity << m.motion.onGround << m.motion.inWater;
}

void encode(QDataStream &stream, QcMsg::SelectHotbarSlot m)
{
    stream << m.slot;
}

void encode(QDataStream &stream, QcMsg::MoveInventoryItem m)
{
    stream << m.from << m.to << m.count;
}

void encode(QDataStream &stream, QcMsg::QuickMoveItem m)
{
    stream << m.slot;
}

void encode(QDataStream &stream, const QcMsg::InventoryState &m)
{
    writeStacks(stream, m.stacks);
    stream << m.selected;
    writeStacks(stream, m.craftingGrid);
    stream << m.craftingSize;
    writeStack(stream, m.craftingResult);
    writeEquipment(stream, m.equipment);
}

void encode(QDataStream &, QcMsg::TakeCraftingResult) {}

void encode(QDataStream &stream, const QcMsg::CraftRecipe &m)
{
    stream << m.recipe << m.count;
}

void encode(QDataStream &, QcMsg::CloseCrafting) {}

void encode(QDataStream &, QcMsg::ConsumeItem) {}

void encode(QDataStream &stream, QcMsg::OpenCrafting m)
{
    stream << m.size;
}

void encode(QDataStream &stream, const QcMsg::PlayerHealth &m)
{
    stream << m.health << m.maxHealth << m.dead;
}

void encode(QDataStream &, QcMsg::Respawn) {}

void encode(QDataStream &stream, const QcMsg::SetAppearance &m)
{
    stream << m.appearance;
}

void encode(QDataStream &stream, QcMsg::DropItem m)
{
    stream << m.wholeStack;
}

void encode(QDataStream &stream, QcMsg::PlayerFood m)
{
    stream << m.food << m.maxFood;
}

QcResult<QcMessage> decode(Tag<QcMsg::PlayerInput>, QDataStream &stream)
{
    QcPlayerInput in;
    stream >> in.sequence >> in.forward >> in.strafe >> in.jump >> in.sneak >> in.sprint >> in.yaw >> in.pitch;
    if (!std::isfinite(in.forward) || !std::isfinite(in.strafe) || !std::isfinite(in.yaw) || !std::isfinite(in.pitch)) {
        return qcError(u"Non-finite player input"_s);
    }
    return QcMsg::PlayerInput{in};
}

QcResult<QcMessage> decode(Tag<QcMsg::PlayerState>, QDataStream &stream)
{
    QcMsg::PlayerState m;
    stream >> m.lastInputSequence >> m.motion.position >> m.motion.velocity >> m.motion.onGround >> m.motion.inWater;
    if (!isFinite(m.motion.position) || !isFinite(m.motion.velocity)) {
        return qcError(u"Non-finite player state"_s);
    }
    return m;
}

QcResult<QcMessage> decode(Tag<QcMsg::SelectHotbarSlot>, QDataStream &stream)
{
    QcMsg::SelectHotbarSlot m;
    stream >> m.slot;
    return m;
}

QcResult<QcMessage> decode(Tag<QcMsg::MoveInventoryItem>, QDataStream &stream)
{
    QcMsg::MoveInventoryItem m;
    stream >> m.from >> m.to >> m.count;
    return m;
}

QcResult<QcMessage> decode(Tag<QcMsg::QuickMoveItem>, QDataStream &stream)
{
    QcMsg::QuickMoveItem m;
    stream >> m.slot;
    if (m.slot < QcMsg::QuickMoveItem::craftingResultSlot
        || m.slot >= QcMsg::MoveInventoryItem::machineInputSlot + QcMsg::MachineState::maxSlots) {
        return qcError(u"Quick move slot %1 out of range"_s.arg(m.slot));
    }
    return m;
}

QcResult<QcMessage> decode(Tag<QcMsg::InventoryState>, QDataStream &stream)
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
    m.equipment = readEquipment(stream);
    return m;
}

QcResult<QcMessage> decode(Tag<QcMsg::TakeCraftingResult>, QDataStream &)
{
    return QcMsg::TakeCraftingResult{};
}

QcResult<QcMessage> decode(Tag<QcMsg::CraftRecipe>, QDataStream &stream)
{
    QcMsg::CraftRecipe m;
    stream >> m.recipe >> m.count;
    return m;
}

QcResult<QcMessage> decode(Tag<QcMsg::CloseCrafting>, QDataStream &)
{
    return QcMsg::CloseCrafting{};
}

QcResult<QcMessage> decode(Tag<QcMsg::ConsumeItem>, QDataStream &)
{
    return QcMsg::ConsumeItem{};
}

QcResult<QcMessage> decode(Tag<QcMsg::OpenCrafting>, QDataStream &stream)
{
    QcMsg::OpenCrafting m;
    stream >> m.size;
    return m;
}

QcResult<QcMessage> decode(Tag<QcMsg::PlayerHealth>, QDataStream &stream)
{
    QcMsg::PlayerHealth m;
    stream >> m.health >> m.maxHealth >> m.dead;
    if (!std::isfinite(m.health) || !std::isfinite(m.maxHealth)) {
        return qcError(u"Non-finite health"_s);
    }
    return m;
}

QcResult<QcMessage> decode(Tag<QcMsg::Respawn>, QDataStream &)
{
    return QcMsg::Respawn{};
}

QcResult<QcMessage> decode(Tag<QcMsg::SetAppearance>, QDataStream &stream)
{
    QcMsg::SetAppearance m;
    if (!readAppearance(stream, m.appearance)) {
        return qcError(u"Invalid appearance"_s);
    }
    return m;
}

QcResult<QcMessage> decode(Tag<QcMsg::DropItem>, QDataStream &stream)
{
    QcMsg::DropItem m;
    stream >> m.wholeStack;
    return m;
}

QcResult<QcMessage> decode(Tag<QcMsg::PlayerFood>, QDataStream &stream)
{
    QcMsg::PlayerFood m;
    stream >> m.food >> m.maxFood;
    if (m.maxFood < 0 || m.maxFood > 1000 || m.food < 0 || m.food > m.maxFood) {
        return qcError(u"Invalid food value"_s);
    }
    return m;
}

} // namespace QcProtocolCodec
