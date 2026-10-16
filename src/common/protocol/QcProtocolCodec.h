#pragma once

// Payload encoders and decoders of the protocol messages, one overload pair per message, grouped by
// topic in QcSessionCodec.cpp, QcWorldCodec.cpp, QcPlayerCodec.cpp, QcEntityCodec.cpp,
// QcMachineCodec.cpp, QcDebugCodec.cpp and QcLoginCodec.cpp; QcProtocol.cpp dispatches on the type tag. Internal to the protocol.

#include "QcMessages.h"
#include "QcResult.h"

#include <QDataStream>

#include <type_traits>

namespace QcProtocolCodec {

// Shared field codecs.
QDataStream &operator<<(QDataStream &stream, QcVec3 value);
QDataStream &operator>>(QDataStream &stream, QcVec3 &value);
QDataStream &operator<<(QDataStream &stream, const QcAppearance &value);
// Names longer than the limit fail; colours keep only their RGB bits.
[[nodiscard]] bool readAppearance(QDataStream &stream, QcAppearance &value);
void writePos(QDataStream &stream, QcBlockPos pos);
QcBlockPos readPos(QDataStream &stream);
void writeStack(QDataStream &stream, QcItemStack stack);
QcItemStack readStack(QDataStream &stream);
void writeStacks(QDataStream &stream, const QList<QcItemStack> &stacks);
// At most 1024 stacks.
bool readStacks(QDataStream &stream, QList<QcItemStack> &stacks);
// The armour slots: always QcEquipment::slotCount stacks, without a count.
void writeEquipment(QDataStream &stream, const QcEquipment &equipment);
// Rotations are normalised on reading (QcStandPose::normalized).
void writeStandPose(QDataStream &stream, const QcStandPose &pose);
QcStandPose readStandPose(QDataStream &stream);
QcEquipment readEquipment(QDataStream &stream);
bool isFinite(const QcVec3 &value);

template<typename T>
using Tag = std::type_identity<T>;

// Session (QcSessionCodec.cpp)
void encode(QDataStream &stream, const QcMsg::Hello &m);
void encode(QDataStream &stream, const QcMsg::Welcome &m);
void encode(QDataStream &stream, const QcMsg::ContentSync &m);
void encode(QDataStream &stream, const QcMsg::Disconnect &m);
void encode(QDataStream &stream, const QcMsg::ChatInput &m);
void encode(QDataStream &stream, const QcMsg::ChatMessage &m);
void encode(QDataStream &stream, const QcMsg::WorldTime &m);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::Hello>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::Welcome>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ContentSync>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::Disconnect>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ChatInput>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ChatMessage>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::WorldTime>, QDataStream &stream);

// World (QcWorldCodec.cpp)
void encode(QDataStream &stream, const QcMsg::ChunkData &m);
void encode(QDataStream &stream, QcMsg::ChunkUnload m);
void encode(QDataStream &stream, QcMsg::BlockUpdate m);
void encode(QDataStream &stream, QcMsg::StartMining m);
void encode(QDataStream &stream, QcMsg::CancelMining m);
void encode(QDataStream &stream, QcMsg::FinishMining m);
void encode(QDataStream &stream, const QcMsg::UseItem &m);
void encode(QDataStream &stream, const QcMsg::SoundEvent &m);
void encode(QDataStream &stream, const QcMsg::ChunkLabels &m);
void encode(QDataStream &stream, QcMsg::Weather m);
void encode(QDataStream &stream, const QcMsg::Lightning &m);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ChunkData>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ChunkUnload>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::BlockUpdate>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::StartMining>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CancelMining>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::FinishMining>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::UseItem>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::SoundEvent>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ChunkLabels>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::Weather>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::Lightning>, QDataStream &stream);

// Player, inventory and crafting (QcPlayerCodec.cpp)
void encode(QDataStream &stream, const QcMsg::PlayerInput &m);
void encode(QDataStream &stream, const QcMsg::PlayerState &m);
void encode(QDataStream &stream, QcMsg::SelectHotbarSlot m);
void encode(QDataStream &stream, QcMsg::MoveInventoryItem m);
void encode(QDataStream &stream, QcMsg::QuickMoveItem m);
void encode(QDataStream &stream, const QcMsg::InventoryState &m);
void encode(QDataStream &stream, QcMsg::TakeCraftingResult m);
void encode(QDataStream &stream, const QcMsg::CraftRecipe &m);
void encode(QDataStream &stream, QcMsg::CloseCrafting m);
void encode(QDataStream &stream, QcMsg::ConsumeItem m);
void encode(QDataStream &stream, QcMsg::OpenCrafting m);
void encode(QDataStream &stream, const QcMsg::PlayerHealth &m);
void encode(QDataStream &stream, QcMsg::Respawn m);
void encode(QDataStream &stream, const QcMsg::SetAppearance &m);
void encode(QDataStream &stream, QcMsg::DropItem m);
void encode(QDataStream &stream, QcMsg::PlayerFood m);
void encode(QDataStream &stream, QcMsg::GameMode m);
void encode(QDataStream &stream, QcMsg::CreativeItem m);
void encode(QDataStream &stream, QcMsg::PickBlock m);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::PlayerInput>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::PlayerState>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::SelectHotbarSlot>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::MoveInventoryItem>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::QuickMoveItem>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::InventoryState>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::TakeCraftingResult>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CraftRecipe>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CloseCrafting>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ConsumeItem>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::OpenCrafting>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::PlayerHealth>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::Respawn>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::SetAppearance>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::DropItem>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::PlayerFood>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::GameMode>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CreativeItem>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::PickBlock>, QDataStream &stream);

// Entities (QcEntityCodec.cpp)
void encode(QDataStream &stream, const QcMsg::EntitySpawn &m);
void encode(QDataStream &stream, const QcMsg::EntityMoves &m);
void encode(QDataStream &stream, QcMsg::EntityRemove m);
void encode(QDataStream &stream, QcMsg::AttackEntity m);
void encode(QDataStream &stream, QcMsg::UseEntity m);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::EntitySpawn>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::EntityMoves>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::EntityRemove>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AttackEntity>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::UseEntity>, QDataStream &stream);

// Machines and chests (QcMachineCodec.cpp)
void encode(QDataStream &stream, const QcMsg::OpenMachine &m);
void encode(QDataStream &stream, const QcMsg::MachineState &m);
void encode(QDataStream &stream, QcMsg::CloseMachine m);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::OpenMachine>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::MachineState>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CloseMachine>, QDataStream &stream);

// Debug display and statistics (QcDebugCodec.cpp)
void encode(QDataStream &stream, QcMsg::DebugSubscribe m);
void encode(QDataStream &stream, const QcMsg::ServerDebug &m);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::DebugSubscribe>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ServerDebug>, QDataStream &stream);

// Login and the player list (QcLoginCodec.cpp)
void encode(QDataStream &stream, const QcMsg::LoginChallenge &m);
void encode(QDataStream &stream, const QcMsg::LoginResponse &m);
void encode(QDataStream &stream, const QcMsg::PlayerList &m);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::LoginChallenge>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::LoginResponse>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::PlayerList>, QDataStream &stream);

} // namespace QcProtocolCodec
