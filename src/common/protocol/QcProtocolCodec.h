// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"
#include "common/protocol/QcMessages.h"

#include <QDataStream>

#include <type_traits>

/*! \brief Payload encoders and decoders of the protocol messages, one overload pair per message.

    They are grouped by topic in QcSessionCodec.cpp, QcWorldCodec.cpp, QcPlayerCodec.cpp,
    QcEntityCodec.cpp, QcMachineCodec.cpp, QcTradeCodec.cpp, QcAdvancementCodec.cpp,
    QcDebugCodec.cpp, QcLoginCodec.cpp, QcSignCodec.cpp, QcMapCodec.cpp and QcAdminCodec.cpp;
    QcProtocol.cpp dispatches on the type tag. Internal to the protocol. */
namespace QcProtocolCodec {

// Shared field codecs.

/*! Writes the three coordinates of \a value to \a stream and returns \a stream. */
QDataStream &operator<<(QDataStream &stream, QcVec3 value);
/*! Reads the three coordinates of \a value from \a stream and returns \a stream. */
QDataStream &operator>>(QDataStream &stream, QcVec3 &value);
/*! Writes the outfit, hair, colours and cape of \a value to \a stream and returns \a stream. */
QDataStream &operator<<(QDataStream &stream, const QcAppearance &value);
/*! Returns the appearance read from \a stream. Names longer than the limit fail
    (std::nullopt); colours keep only their RGB bits. */
[[nodiscard]] std::optional<QcAppearance> readAppearance(QDataStream &stream);
/*! Writes \a pos to \a stream as three 32-bit integers. */
void writePos(QDataStream &stream, QcBlockPos pos);
/*! Returns the block position read from \a stream (three 32-bit integers). */
QcBlockPos readPos(QDataStream &stream);
/*! Writes \a stack to \a stream: item, count and damage, then the known components of its data
    (decision E-823) when it has any. */
void writeStack(QDataStream &stream, const QcItemStack &stack);
/*! Returns the stack read from \a stream with the known components of its data (decision E-823):
    contents of at most QcItemData::maxContents stacks nesting at most QcItemData::maxDepth levels
    and holding at most QcItemData::maxStacksInside stacks, names of at most
    QcItemData::maxNameLength characters, 1 to QcItemData::maxEnchantments enchantments (names of
    1 to maxEnchantmentName characters, levels of at least 1), prior work 1 to maxPriorWork;
    unknown component bits or anything beyond the limits turns the stream corrupt and returns an
    empty stack. */
QcItemStack readStack(QDataStream &stream);
/*! Returns a stack from a client read from \a stream: as readStack(), but contents turn the
    stream corrupt (a client never sends stacks inside a stack). */
QcItemStack readFlatStack(QDataStream &stream);
/*! Writes the number of \a stacks and then each stack to \a stream. */
void writeStacks(QDataStream &stream, const QList<QcItemStack> &stacks);
/*! Returns the stacks read from \a stream: at most 1024; std::nullopt for a larger count. */
[[nodiscard]] std::optional<QList<QcItemStack>> readStacks(QDataStream &stream);
/*! Writes the armour slots of \a equipment to \a stream: always QcEquipment::slotCount stacks,
    without a count. */
void writeEquipment(QDataStream &stream, const QcEquipment &equipment);
/*! Writes the rotations of the parts of \a pose and its flags to \a stream. */
void writeStandPose(QDataStream &stream, const QcStandPose &pose);
/*! Returns the stand pose read from \a stream; rotations are normalised
    (QcStandPose::normalized). */
QcStandPose readStandPose(QDataStream &stream);
/*! Returns the armour slots read from \a stream (QcEquipment::slotCount stacks). */
QcEquipment readEquipment(QDataStream &stream);
/*! Returns whether all three coordinates of \a value are finite. */
bool isFinite(const QcVec3 &value);

/*! The tag type that selects the decode() overload for the message type \c T. */
template<typename T>
using Tag = std::type_identity<T>;

// Session (QcSessionCodec.cpp)

/*! Writes the payload of \a m (QcMsg::Hello) to \a stream. */
void encode(QDataStream &stream, const QcMsg::Hello &m);
/*! Writes the payload of \a m (QcMsg::Welcome) to \a stream. */
void encode(QDataStream &stream, const QcMsg::Welcome &m);
/*! Writes the payload of \a m (QcMsg::ContentSync) to \a stream. */
void encode(QDataStream &stream, const QcMsg::ContentSync &m);
/*! Writes the payload of \a m (QcMsg::Disconnect) to \a stream. */
void encode(QDataStream &stream, const QcMsg::Disconnect &m);
/*! Writes the payload of \a m (QcMsg::ChatInput) to \a stream. */
void encode(QDataStream &stream, const QcMsg::ChatInput &m);
/*! Writes the payload of \a m (QcMsg::ChatMessage) to \a stream. */
void encode(QDataStream &stream, const QcMsg::ChatMessage &m);
/*! Writes the payload of \a m (QcMsg::WorldTime) to \a stream. */
void encode(QDataStream &stream, const QcMsg::WorldTime &m);
/*! Returns the QcMsg::Hello read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::Hello>, QDataStream &stream);
/*! Returns the QcMsg::Welcome read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::Welcome>, QDataStream &stream);
/*! Returns the QcMsg::ContentSync read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ContentSync>, QDataStream &stream);
/*! Returns the QcMsg::Disconnect read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::Disconnect>, QDataStream &stream);
/*! Returns the QcMsg::ChatInput read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ChatInput>, QDataStream &stream);
/*! Returns the QcMsg::ChatMessage read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ChatMessage>, QDataStream &stream);
/*! Returns the QcMsg::WorldTime read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::WorldTime>, QDataStream &stream);
/*! Writes the payload of \a m (QcMsg::Ping) to \a stream. */
void encode(QDataStream &stream, QcMsg::Ping m);
/*! Writes the payload of \a m (QcMsg::Pong) to \a stream. */
void encode(QDataStream &stream, QcMsg::Pong m);
/*! Returns the QcMsg::Ping read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::Ping>, QDataStream &stream);
/*! Returns the QcMsg::Pong read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::Pong>, QDataStream &stream);
/*! Writes the payload of \a m (QcMsg::PauseRequest) to \a stream. */
void encode(QDataStream &stream, QcMsg::PauseRequest m);
/*! Writes the payload of \a m (QcMsg::ServerPause) to \a stream. */
void encode(QDataStream &stream, QcMsg::ServerPause m);
/*! Returns the QcMsg::PauseRequest read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::PauseRequest>, QDataStream &stream);
/*! Returns the QcMsg::ServerPause read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ServerPause>, QDataStream &stream);
/*! Writes the payload of \a m (QcMsg::LatencyProbe) to \a stream. */
void encode(QDataStream &stream, QcMsg::LatencyProbe m);
/*! Writes the payload of \a m (QcMsg::LatencyEcho) to \a stream. */
void encode(QDataStream &stream, QcMsg::LatencyEcho m);
/*! Returns the QcMsg::LatencyProbe read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::LatencyProbe>, QDataStream &stream);
/*! Returns the QcMsg::LatencyEcho read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::LatencyEcho>, QDataStream &stream);

// World (QcWorldCodec.cpp)

/*! Writes the payload of \a m (QcMsg::ChunkData) to \a stream. */
void encode(QDataStream &stream, const QcMsg::ChunkData &m);
/*! Writes the payload of \a m (QcMsg::ChunkUnload) to \a stream. */
void encode(QDataStream &stream, QcMsg::ChunkUnload m);
/*! Writes the payload of \a m (QcMsg::BlockUpdate) to \a stream. */
void encode(QDataStream &stream, QcMsg::BlockUpdate m);
/*! Writes the payload of \a m (QcMsg::StartMining) to \a stream. */
void encode(QDataStream &stream, QcMsg::StartMining m);
/*! Writes the payload of \a m (QcMsg::CancelMining) to \a stream. */
void encode(QDataStream &stream, QcMsg::CancelMining m);
/*! Writes the payload of \a m (QcMsg::FinishMining) to \a stream. */
void encode(QDataStream &stream, QcMsg::FinishMining m);
/*! Writes the payload of \a m (QcMsg::UseItem) to \a stream. */
void encode(QDataStream &stream, const QcMsg::UseItem &m);
/*! Writes the payload of \a m (QcMsg::SoundEvent) to \a stream. */
void encode(QDataStream &stream, const QcMsg::SoundEvent &m);
/*! Writes the payload of \a m (QcMsg::ChunkLabels) to \a stream. */
void encode(QDataStream &stream, const QcMsg::ChunkLabels &m);
/*! Writes the payload of \a m (QcMsg::Weather) to \a stream. */
void encode(QDataStream &stream, QcMsg::Weather m);
/*! Writes the payload of \a m (QcMsg::Lightning) to \a stream. */
void encode(QDataStream &stream, const QcMsg::Lightning &m);
/*! Writes the payload of \a m (QcMsg::SpawnerDisplay) to \a stream. */
void encode(QDataStream &stream, QcMsg::SpawnerDisplay m);
/*! Writes the payload of \a m (QcMsg::BlockBatch) to \a stream. */
void encode(QDataStream &stream, const QcMsg::BlockBatch &m);
/*! Writes the payload of \a m (QcMsg::Explosion) to \a stream. */
void encode(QDataStream &stream, const QcMsg::Explosion &m);
/*! Writes the payload of \a m (QcMsg::CampfireDisplay) to \a stream. */
void encode(QDataStream &stream, const QcMsg::CampfireDisplay &m);
/*! Writes the payload of \a m (QcMsg::BlockAck) to \a stream. */
void encode(QDataStream &stream, QcMsg::BlockAck m);
/*! Returns the QcMsg::ChunkData read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ChunkData>, QDataStream &stream);
/*! Returns the QcMsg::ChunkUnload read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ChunkUnload>, QDataStream &stream);
/*! Returns the QcMsg::BlockUpdate read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::BlockUpdate>, QDataStream &stream);
/*! Returns the QcMsg::StartMining read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::StartMining>, QDataStream &stream);
/*! Returns the QcMsg::CancelMining read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CancelMining>, QDataStream &stream);
/*! Returns the QcMsg::FinishMining read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::FinishMining>, QDataStream &stream);
/*! Returns the QcMsg::UseItem read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::UseItem>, QDataStream &stream);
/*! Returns the QcMsg::SoundEvent read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::SoundEvent>, QDataStream &stream);
/*! Returns the QcMsg::ChunkLabels read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ChunkLabels>, QDataStream &stream);
/*! Returns the QcMsg::Weather read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::Weather>, QDataStream &stream);
/*! Returns the QcMsg::Lightning read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::Lightning>, QDataStream &stream);
/*! Returns the QcMsg::SpawnerDisplay read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::SpawnerDisplay>, QDataStream &stream);
/*! Returns the QcMsg::BlockBatch read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::BlockBatch>, QDataStream &stream);
/*! Returns the QcMsg::Explosion read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::Explosion>, QDataStream &stream);
/*! Returns the QcMsg::CampfireDisplay read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CampfireDisplay>, QDataStream &stream);
/*! Returns the QcMsg::BlockAck read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::BlockAck>, QDataStream &stream);

// Signs (QcSignCodec.cpp)

/*! Writes the payload of \a m (QcMsg::SignText) to \a stream. */
void encode(QDataStream &stream, const QcMsg::SignText &m);
/*! Writes the payload of \a m (QcMsg::OpenSignEditor) to \a stream. */
void encode(QDataStream &stream, const QcMsg::OpenSignEditor &m);
/*! Writes the payload of \a m (QcMsg::EditSign) to \a stream. */
void encode(QDataStream &stream, const QcMsg::EditSign &m);
/*! Returns the QcMsg::SignText read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::SignText>, QDataStream &stream);
/*! Returns the QcMsg::OpenSignEditor read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::OpenSignEditor>, QDataStream &stream);
/*! Returns the QcMsg::EditSign read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::EditSign>, QDataStream &stream);

// Player, inventory and crafting (QcPlayerCodec.cpp)

/*! Writes the payload of \a m (QcMsg::PlayerInput) to \a stream. */
void encode(QDataStream &stream, const QcMsg::PlayerInput &m);
/*! Writes the payload of \a m (QcMsg::PlayerState) to \a stream. */
void encode(QDataStream &stream, const QcMsg::PlayerState &m);
/*! Writes the payload of \a m (QcMsg::SelectHotbarSlot) to \a stream. */
void encode(QDataStream &stream, QcMsg::SelectHotbarSlot m);
/*! Writes the payload of \a m (QcMsg::MoveInventoryItem) to \a stream. */
void encode(QDataStream &stream, QcMsg::MoveInventoryItem m);
/*! Writes the payload of \a m (QcMsg::QuickMoveItem) to \a stream. */
void encode(QDataStream &stream, QcMsg::QuickMoveItem m);
/*! Writes the payload of \a m (QcMsg::InventoryState) to \a stream. */
void encode(QDataStream &stream, const QcMsg::InventoryState &m);
/*! Writes the payload of \a m (QcMsg::TakeCraftingResult) to \a stream. */
void encode(QDataStream &stream, QcMsg::TakeCraftingResult m);
/*! Writes the payload of \a m (QcMsg::CraftRecipe) to \a stream. */
void encode(QDataStream &stream, const QcMsg::CraftRecipe &m);
/*! Writes the payload of \a m (QcMsg::CloseCrafting) to \a stream. */
void encode(QDataStream &stream, QcMsg::CloseCrafting m);
/*! Writes the payload of \a m (QcMsg::ConsumeItem) to \a stream. */
void encode(QDataStream &stream, QcMsg::ConsumeItem m);
/*! Writes the payload of \a m (QcMsg::OpenCrafting) to \a stream. */
void encode(QDataStream &stream, QcMsg::OpenCrafting m);
/*! Writes the payload of \a m (QcMsg::PlayerHealth) to \a stream. */
void encode(QDataStream &stream, const QcMsg::PlayerHealth &m);
/*! Writes the payload of \a m (QcMsg::Respawn) to \a stream. */
void encode(QDataStream &stream, QcMsg::Respawn m);
/*! Writes the payload of \a m (QcMsg::SetAppearance) to \a stream. */
void encode(QDataStream &stream, const QcMsg::SetAppearance &m);
/*! Writes the payload of \a m (QcMsg::DropItem) to \a stream. */
void encode(QDataStream &stream, QcMsg::DropItem m);
/*! Writes the payload of \a m (QcMsg::PlayerFood) to \a stream. */
void encode(QDataStream &stream, QcMsg::PlayerFood m);
/*! Writes the payload of \a m (QcMsg::PlayerExperience) to \a stream. */
void encode(QDataStream &stream, QcMsg::PlayerExperience m);
/*! Writes the payload of \a m (QcMsg::GameMode) to \a stream. */
void encode(QDataStream &stream, const QcMsg::GameMode &m);
/*! Writes the payload of \a m (QcMsg::CreativeItem) to \a stream. */
void encode(QDataStream &stream, const QcMsg::CreativeItem &m);
/*! Writes the payload of \a m (QcMsg::PickBlock) to \a stream. */
void encode(QDataStream &stream, QcMsg::PickBlock m);
/*! Writes the payload of \a m (QcMsg::DrawLauncher) to \a stream. */
void encode(QDataStream &stream, QcMsg::DrawLauncher m);
/*! Writes the payload of \a m (QcMsg::ThrowItem) to \a stream. */
void encode(QDataStream &stream, QcMsg::ThrowItem m);
/*! Returns the QcMsg::PlayerInput read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::PlayerInput>, QDataStream &stream);
/*! Returns the QcMsg::PlayerState read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::PlayerState>, QDataStream &stream);
/*! Returns the QcMsg::SelectHotbarSlot read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::SelectHotbarSlot>, QDataStream &stream);
/*! Returns the QcMsg::MoveInventoryItem read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::MoveInventoryItem>, QDataStream &stream);
/*! Returns the QcMsg::QuickMoveItem read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::QuickMoveItem>, QDataStream &stream);
/*! Returns the QcMsg::InventoryState read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::InventoryState>, QDataStream &stream);
/*! Returns the QcMsg::TakeCraftingResult read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::TakeCraftingResult>, QDataStream &stream);
/*! Returns the QcMsg::CraftRecipe read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CraftRecipe>, QDataStream &stream);
/*! Returns the QcMsg::CloseCrafting read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CloseCrafting>, QDataStream &stream);
/*! Returns the QcMsg::ConsumeItem read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ConsumeItem>, QDataStream &stream);
/*! Returns the QcMsg::OpenCrafting read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::OpenCrafting>, QDataStream &stream);
/*! Returns the QcMsg::PlayerHealth read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::PlayerHealth>, QDataStream &stream);
/*! Returns the QcMsg::Respawn read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::Respawn>, QDataStream &stream);
/*! Returns the QcMsg::SetAppearance read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::SetAppearance>, QDataStream &stream);
/*! Returns the QcMsg::DropItem read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::DropItem>, QDataStream &stream);
/*! Returns the QcMsg::PlayerFood read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::PlayerFood>, QDataStream &stream);
/*! Returns the QcMsg::PlayerExperience read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::PlayerExperience>, QDataStream &stream);
/*! Returns the QcMsg::GameMode read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::GameMode>, QDataStream &stream);
/*! Returns the QcMsg::CreativeItem read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CreativeItem>, QDataStream &stream);
/*! Returns the QcMsg::PickBlock read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::PickBlock>, QDataStream &stream);
/*! Returns the QcMsg::DrawLauncher read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::DrawLauncher>, QDataStream &stream);
/*! Returns the QcMsg::ThrowItem read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ThrowItem>, QDataStream &stream);

// Trading (QcTradeCodec.cpp)

/*! Writes the payload of \a m (QcMsg::TradeState) to \a stream. */
void encode(QDataStream &stream, const QcMsg::TradeState &m);
/*! Writes the payload of \a m (QcMsg::Trade) to \a stream. */
void encode(QDataStream &stream, QcMsg::Trade m);
/*! Writes the payload of \a m (QcMsg::CloseTrade) to \a stream. */
void encode(QDataStream &stream, QcMsg::CloseTrade m);
/*! Returns the QcMsg::TradeState read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::TradeState>, QDataStream &stream);
/*! Returns the QcMsg::Trade read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::Trade>, QDataStream &stream);
/*! Returns the QcMsg::CloseTrade read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CloseTrade>, QDataStream &stream);

// Entities (QcEntityCodec.cpp)

/*! Writes the payload of \a m (QcMsg::EntitySpawn) to \a stream. */
void encode(QDataStream &stream, const QcMsg::EntitySpawn &m);
/*! Writes the payload of \a m (QcMsg::EntityMoves) to \a stream. */
void encode(QDataStream &stream, const QcMsg::EntityMoves &m);
/*! Writes the payload of \a m (QcMsg::EntityRemove) to \a stream. */
void encode(QDataStream &stream, QcMsg::EntityRemove m);
/*! Writes the payload of \a m (QcMsg::AttackEntity) to \a stream. */
void encode(QDataStream &stream, QcMsg::AttackEntity m);
/*! Writes the payload of \a m (QcMsg::UseEntity) to \a stream. */
void encode(QDataStream &stream, QcMsg::UseEntity m);
/*! Returns the QcMsg::EntitySpawn read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::EntitySpawn>, QDataStream &stream);
/*! Returns the QcMsg::EntityMoves read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::EntityMoves>, QDataStream &stream);
/*! Returns the QcMsg::EntityRemove read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::EntityRemove>, QDataStream &stream);
/*! Returns the QcMsg::AttackEntity read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AttackEntity>, QDataStream &stream);
/*! Returns the QcMsg::UseEntity read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::UseEntity>, QDataStream &stream);

// Machines and chests (QcMachineCodec.cpp)

/*! Writes the payload of \a m (QcMsg::OpenMachine) to \a stream. */
void encode(QDataStream &stream, const QcMsg::OpenMachine &m);
/*! Writes the payload of \a m (QcMsg::MachineState) to \a stream. */
void encode(QDataStream &stream, const QcMsg::MachineState &m);
/*! Writes the payload of \a m (QcMsg::CloseMachine) to \a stream. */
void encode(QDataStream &stream, QcMsg::CloseMachine m);
/*! Returns the QcMsg::OpenMachine read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::OpenMachine>, QDataStream &stream);
/*! Returns the QcMsg::MachineState read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::MachineState>, QDataStream &stream);
/*! Returns the QcMsg::CloseMachine read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CloseMachine>, QDataStream &stream);
/*! Writes the payload of \a m (QcMsg::OpenStonecutter) to \a stream. */
void encode(QDataStream &stream, QcMsg::OpenStonecutter m);
/*! Writes the payload of \a m (QcMsg::StonecutterState) to \a stream. */
void encode(QDataStream &stream, const QcMsg::StonecutterState &m);
/*! Writes the payload of \a m (QcMsg::SelectStonecutterRecipe) to \a stream. */
void encode(QDataStream &stream, const QcMsg::SelectStonecutterRecipe &m);
/*! Writes the payload of \a m (QcMsg::CloseStonecutter) to \a stream. */
void encode(QDataStream &stream, QcMsg::CloseStonecutter m);
/*! Returns the QcMsg::OpenStonecutter read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::OpenStonecutter>, QDataStream &stream);
/*! Returns the QcMsg::StonecutterState read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::StonecutterState>, QDataStream &stream);
/*! Returns the QcMsg::SelectStonecutterRecipe read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::SelectStonecutterRecipe>, QDataStream &stream);
/*! Returns the QcMsg::CloseStonecutter read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CloseStonecutter>, QDataStream &stream);
/*! Writes the payload of \a m (QcMsg::OpenEnchanting) to \a stream. */
void encode(QDataStream &stream, QcMsg::OpenEnchanting m);
/*! Writes the payload of \a m (QcMsg::EnchantingState) to \a stream. */
void encode(QDataStream &stream, const QcMsg::EnchantingState &m);
/*! Writes the payload of \a m (QcMsg::ChooseEnchantment) to \a stream. */
void encode(QDataStream &stream, QcMsg::ChooseEnchantment m);
/*! Writes the payload of \a m (QcMsg::CloseEnchanting) to \a stream. */
void encode(QDataStream &stream, QcMsg::CloseEnchanting m);
/*! Returns the QcMsg::OpenEnchanting read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::OpenEnchanting>, QDataStream &stream);
/*! Returns the QcMsg::EnchantingState read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::EnchantingState>, QDataStream &stream);
/*! Returns the QcMsg::ChooseEnchantment read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ChooseEnchantment>, QDataStream &stream);
/*! Returns the QcMsg::CloseEnchanting read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CloseEnchanting>, QDataStream &stream);
/*! Writes the payload of \a m (QcMsg::OpenAnvil) to \a stream. */
void encode(QDataStream &stream, QcMsg::OpenAnvil m);
/*! Writes the payload of \a m (QcMsg::AnvilState) to \a stream. */
void encode(QDataStream &stream, const QcMsg::AnvilState &m);
/*! Writes the payload of \a m (QcMsg::RenameAnvil) to \a stream. */
void encode(QDataStream &stream, const QcMsg::RenameAnvil &m);
/*! Writes the payload of \a m (QcMsg::ForgeAnvil) to \a stream. */
void encode(QDataStream &stream, const QcMsg::ForgeAnvil &m);
/*! Writes the payload of \a m (QcMsg::CloseAnvil) to \a stream. */
void encode(QDataStream &stream, QcMsg::CloseAnvil m);
/*! Returns the QcMsg::OpenAnvil read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::OpenAnvil>, QDataStream &stream);
/*! Returns the QcMsg::AnvilState read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AnvilState>, QDataStream &stream);
/*! Returns the QcMsg::RenameAnvil read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::RenameAnvil>, QDataStream &stream);
/*! Returns the QcMsg::ForgeAnvil read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ForgeAnvil>, QDataStream &stream);
/*! Returns the QcMsg::CloseAnvil read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CloseAnvil>, QDataStream &stream);

// Debug display and statistics (QcDebugCodec.cpp)

/*! Writes the payload of \a m (QcMsg::DebugSubscribe) to \a stream. */
void encode(QDataStream &stream, QcMsg::DebugSubscribe m);
/*! Writes the payload of \a m (QcMsg::ServerDebug) to \a stream. */
void encode(QDataStream &stream, const QcMsg::ServerDebug &m);
/*! Returns the QcMsg::DebugSubscribe read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::DebugSubscribe>, QDataStream &stream);
/*! Returns the QcMsg::ServerDebug read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ServerDebug>, QDataStream &stream);

// Login and the player list (QcLoginCodec.cpp)

/*! Writes the payload of \a m (QcMsg::LoginChallenge) to \a stream. */
void encode(QDataStream &stream, const QcMsg::LoginChallenge &m);
/*! Writes the payload of \a m (QcMsg::LoginResponse) to \a stream. */
void encode(QDataStream &stream, const QcMsg::LoginResponse &m);
/*! Writes the payload of \a m (QcMsg::PlayerList) to \a stream. */
void encode(QDataStream &stream, const QcMsg::PlayerList &m);
/*! Returns the QcMsg::LoginChallenge read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::LoginChallenge>, QDataStream &stream);
/*! Returns the QcMsg::LoginResponse read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::LoginResponse>, QDataStream &stream);
/*! Returns the QcMsg::PlayerList read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::PlayerList>, QDataStream &stream);

// Advancements (QcAdvancementCodec.cpp)

/*! Writes the payload of \a m (QcMsg::AdvancementProgress) to \a stream. */
void encode(QDataStream &stream, const QcMsg::AdvancementProgress &m);
/*! Returns the QcMsg::AdvancementProgress read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdvancementProgress>, QDataStream &stream);

// Maps (QcMapCodec.cpp)

/*! Writes the payload of \a m (QcMsg::MapUpdate) to \a stream. */
void encode(QDataStream &stream, const QcMsg::MapUpdate &m);
/*! Returns the QcMsg::MapUpdate read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::MapUpdate>, QDataStream &stream);

// Admin channel (QcAdminCodec.cpp)

/*! Writes the payload of \a m (QcMsg::AdminSubscribe) to \a stream. */
void encode(QDataStream &stream, QcMsg::AdminSubscribe m);
/*! Writes the payload of \a m (QcMsg::AdminRequest) to \a stream. */
void encode(QDataStream &stream, const QcMsg::AdminRequest &m);
/*! Writes the payload of \a m (QcMsg::AdminResult) to \a stream. */
void encode(QDataStream &stream, const QcMsg::AdminResult &m);
/*! Writes the payload of \a m (QcMsg::AdminStatus) to \a stream. */
void encode(QDataStream &stream, const QcMsg::AdminStatus &m);
/*! Writes the payload of \a m (QcMsg::AdminHistory) to \a stream. */
void encode(QDataStream &stream, const QcMsg::AdminHistory &m);
/*! Writes the payload of \a m (QcMsg::AdminPlayers) to \a stream. */
void encode(QDataStream &stream, const QcMsg::AdminPlayers &m);
/*! Writes the payload of \a m (QcMsg::AdminChat) to \a stream. */
void encode(QDataStream &stream, const QcMsg::AdminChat &m);
/*! Writes the payload of \a m (QcMsg::AdminCatalogue) to \a stream. */
void encode(QDataStream &stream, const QcMsg::AdminCatalogue &m);
/*! Writes the payload of \a m (QcMsg::AdminLists) to \a stream. */
void encode(QDataStream &stream, const QcMsg::AdminLists &m);
/*! Writes the payload of \a m (QcMsg::AdminSettings) to \a stream. */
void encode(QDataStream &stream, const QcMsg::AdminSettings &m);
/*! Writes the payload of \a m (QcMsg::AdminLog) to \a stream. */
void encode(QDataStream &stream, const QcMsg::AdminLog &m);
/*! Returns the QcMsg::AdminSubscribe read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminSubscribe>, QDataStream &stream);
/*! Returns the QcMsg::AdminRequest read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminRequest>, QDataStream &stream);
/*! Returns the QcMsg::AdminResult read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminResult>, QDataStream &stream);
/*! Returns the QcMsg::AdminStatus read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminStatus>, QDataStream &stream);
/*! Returns the QcMsg::AdminHistory read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminHistory>, QDataStream &stream);
/*! Returns the QcMsg::AdminPlayers read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminPlayers>, QDataStream &stream);
/*! Returns the QcMsg::AdminChat read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminChat>, QDataStream &stream);
/*! Returns the QcMsg::AdminCatalogue read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminCatalogue>, QDataStream &stream);
/*! Returns the QcMsg::AdminLists read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminLists>, QDataStream &stream);
/*! Returns the QcMsg::AdminSettings read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminSettings>, QDataStream &stream);
/*! Returns the QcMsg::AdminLog read from \a stream, or why it is invalid. */
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminLog>, QDataStream &stream);

} // namespace QcProtocolCodec
