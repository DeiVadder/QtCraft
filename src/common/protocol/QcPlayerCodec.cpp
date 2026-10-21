// Payloads of the player messages (QcPlayerMessages.h): input and state, inventory, crafting,
// health, food and appearance.

#include "common/gameplay/QcExperience.h"
#include "common/gameplay/QcInventory.h"
#include "common/protocol/QcProtocolCodec.h"

#include <cmath>

using namespace Qt::StringLiterals;

static_assert(QcMsg::MoveInventoryItem::equipmentSlot >= QcMsg::MoveInventoryItem::machineInputSlot + QcMsg::MachineState::maxSlots,
              "armour slots follow every block slot");

namespace QcProtocolCodec {

void encode(QDataStream &stream, const QcMsg::PlayerInput &m)
{
    const QcPlayerInput &in = m.input;
    stream << in.sequence << in.forward << in.strafe << in.jump << in.sneak << in.sprint << in.flying << in.flySpeed << in.yaw << in.pitch;
}

void encode(QDataStream &stream, const QcMsg::PlayerState &m)
{
    stream << m.lastInputSequence << m.motion.position << m.motion.velocity << m.motion.onGround << m.motion.inWater << m.motion.flying
           << m.vehicle;
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

void encode(QDataStream &, QcMsg::ThrowItem) {}

void encode(QDataStream &stream, QcMsg::OpenCrafting m)
{
    stream << m.size;
}

void encode(QDataStream &stream, const QcMsg::PlayerHealth &m)
{
    stream << m.health << m.maxHealth << m.dead << m.burning;
    QcStatusEffects::write(stream, m.effects);
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

void encode(QDataStream &stream, QcMsg::PlayerExperience m)
{
    stream << m.level << m.points << m.pointsForNext;
}

void encode(QDataStream &stream, const QcMsg::GameMode &m)
{
    stream << quint8(m.mode);
    m.adventure.write(stream);
}

void encode(QDataStream &stream, const QcMsg::CreativeItem &m)
{
    stream << m.slot;
    writeStack(stream, m.stack);
}

void encode(QDataStream &stream, QcMsg::PickBlock m)
{
    writePos(stream, m.pos);
}

void encode(QDataStream &stream, QcMsg::DrawLauncher m)
{
    stream << quint8(m.action);
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::PlayerInput>, QDataStream &stream)
{
    QcPlayerInput in;
    stream >> in.sequence >> in.forward >> in.strafe >> in.jump >> in.sneak >> in.sprint >> in.flying >> in.flySpeed >> in.yaw >> in.pitch;
    if (!std::isfinite(in.forward) || !std::isfinite(in.strafe) || !std::isfinite(in.yaw) || !std::isfinite(in.pitch)) {
        return qcError(u"Non-finite player input"_s);
    }
    if (in.flySpeed < QcPlayerInput::minFlySpeed || in.flySpeed > QcPlayerInput::maxFlySpeed) {
        return qcError(u"Flight speed %1 out of range"_s.arg(in.flySpeed));
    }
    return QcMsg::PlayerInput{in};
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::PlayerState>, QDataStream &stream)
{
    QcMsg::PlayerState m;
    stream >> m.lastInputSequence >> m.motion.position >> m.motion.velocity >> m.motion.onGround >> m.motion.inWater >> m.motion.flying
        >> m.vehicle;
    if (!isFinite(m.motion.position) || !isFinite(m.motion.velocity)) {
        return qcError(u"Non-finite player state"_s);
    }
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::SelectHotbarSlot>, QDataStream &stream)
{
    QcMsg::SelectHotbarSlot m;
    stream >> m.slot;
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::MoveInventoryItem>, QDataStream &stream)
{
    QcMsg::MoveInventoryItem m;
    stream >> m.from >> m.to >> m.count;
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::QuickMoveItem>, QDataStream &stream)
{
    QcMsg::QuickMoveItem m;
    stream >> m.slot;
    if ((m.slot < QcMsg::QuickMoveItem::craftingResultSlot
         || m.slot >= QcMsg::MoveInventoryItem::machineInputSlot + QcMsg::MachineState::maxSlots)
        && !QcMsg::MoveInventoryItem::isEquipmentSlot(m.slot)) {
        return qcError(u"Quick move slot %1 out of range"_s.arg(m.slot));
    }
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::InventoryState>, QDataStream &stream)
{
    QcMsg::InventoryState m;
    std::optional<QList<QcItemStack>> stacks = readStacks(stream);
    if (!stacks) {
        return qcError(u"Corrupt inventory"_s);
    }
    m.stacks = std::move(*stacks);
    stream >> m.selected;
    std::optional<QList<QcItemStack>> craftingGrid = readStacks(stream);
    if (!craftingGrid) {
        return qcError(u"Corrupt crafting grid"_s);
    }
    m.craftingGrid = std::move(*craftingGrid);
    stream >> m.craftingSize;
    m.craftingResult = readStack(stream);
    m.equipment = readEquipment(stream);
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::TakeCraftingResult>, QDataStream &)
{
    return QcMsg::TakeCraftingResult{};
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CraftRecipe>, QDataStream &stream)
{
    QcMsg::CraftRecipe m;
    stream >> m.recipe >> m.count;
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CloseCrafting>, QDataStream &)
{
    return QcMsg::CloseCrafting{};
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ConsumeItem>, QDataStream &)
{
    return QcMsg::ConsumeItem{};
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ThrowItem>, QDataStream &)
{
    return QcMsg::ThrowItem{};
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::OpenCrafting>, QDataStream &stream)
{
    QcMsg::OpenCrafting m;
    stream >> m.size;
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::PlayerHealth>, QDataStream &stream)
{
    QcMsg::PlayerHealth m;
    stream >> m.health >> m.maxHealth >> m.dead >> m.burning;
    if (!std::isfinite(m.health) || !std::isfinite(m.maxHealth)) {
        return qcError(u"Non-finite health"_s);
    }
    std::optional<QList<QcStatusEffect>> effects = QcStatusEffects::read(stream);
    if (!effects) {
        return qcError(u"Damaged status effects"_s);
    }
    m.effects = std::move(*effects);
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::Respawn>, QDataStream &)
{
    return QcMsg::Respawn{};
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::SetAppearance>, QDataStream &stream)
{
    QcMsg::SetAppearance m;
    std::optional<QcAppearance> appearance = readAppearance(stream);
    if (!appearance) {
        return qcError(u"Invalid appearance"_s);
    }
    m.appearance = std::move(*appearance);
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::DropItem>, QDataStream &stream)
{
    QcMsg::DropItem m;
    stream >> m.wholeStack;
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::PlayerFood>, QDataStream &stream)
{
    QcMsg::PlayerFood m;
    stream >> m.food >> m.maxFood;
    if (m.maxFood < 0 || m.maxFood > 1000 || m.food < 0 || m.food > m.maxFood) {
        return qcError(u"Invalid food value"_s);
    }
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::PlayerExperience>, QDataStream &stream)
{
    QcMsg::PlayerExperience m;
    stream >> m.level >> m.points >> m.pointsForNext;
    if (m.level < 0 || m.level > QcExperience::maxLevel || m.pointsForNext <= 0 || m.points < 0 || m.points >= m.pointsForNext) {
        return qcError(u"Invalid experience"_s);
    }
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::GameMode>, QDataStream &stream)
{
    quint8 value = 0;
    stream >> value;
    const std::optional<QcGameMode> mode = QcGameModes::fromValue(value);
    if (!mode) {
        return qcError(u"Unknown game mode %1"_s.arg(value));
    }
    std::optional<QcAdventureRules> adventure = QcAdventureRules::read(stream);
    if (!adventure) {
        return qcError(u"Invalid adventure allow-lists"_s);
    }
    return QcMsg::GameMode{*mode, std::move(*adventure)};
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CreativeItem>, QDataStream &stream)
{
    QcMsg::CreativeItem m;
    stream >> m.slot;
    m.stack = readFlatStack(stream);
    const bool inventorySlot = m.slot >= QcMsg::CreativeItem::addSlot && m.slot < QcInventory::slotCount;
    if (!inventorySlot && !QcMsg::MoveInventoryItem::isEquipmentSlot(m.slot)) {
        return qcError(u"Creative slot %1 out of range"_s.arg(m.slot));
    }
    if (m.stack.count < 0 || m.stack.count > QcItemDefinition::maxStackLimit || m.stack.damage < 0) {
        return qcError(u"Invalid creative stack"_s);
    }
    return m;
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::PickBlock>, QDataStream &stream)
{
    return QcMsg::PickBlock{readPos(stream)};
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::DrawLauncher>, QDataStream &stream)
{
    quint8 action = 0;
    stream >> action;
    if (action > quint8(QcMsg::DrawLauncher::Action::Cancel)) {
        return qcError(u"Unknown launcher action %1"_s.arg(action));
    }
    return QcMsg::DrawLauncher{QcMsg::DrawLauncher::Action(action)};
}

} // namespace QcProtocolCodec
