#pragma once

// Payload encoders and decoders of the protocol messages, one overload pair per message, grouped by
// topic in QcSessionCodec.cpp, QcWorldCodec.cpp, QcPlayerCodec.cpp, QcEntityCodec.cpp,
// QcMachineCodec.cpp, QcTradeCodec.cpp, QcDebugCodec.cpp, QcLoginCodec.cpp, QcSignCodec.cpp, QcMapCodec.cpp and QcAdminCodec.cpp; QcProtocol.cpp dispatches on the type tag. Internal to the protocol.

#include "common/core/QcResult.h"
#include "common/protocol/QcMessages.h"

#include <QDataStream>

#include <type_traits>

namespace QcProtocolCodec {

// Shared field codecs.
QDataStream &operator<<(QDataStream &stream, QcVec3 value);
QDataStream &operator>>(QDataStream &stream, QcVec3 &value);
QDataStream &operator<<(QDataStream &stream, const QcAppearance &value);
// Names longer than the limit fail (std::nullopt); colours keep only their RGB bits.
[[nodiscard]] std::optional<QcAppearance> readAppearance(QDataStream &stream);
void writePos(QDataStream &stream, QcBlockPos pos);
QcBlockPos readPos(QDataStream &stream);
// A stack with the known components of its data (decision E-823): contents of at most
// QcItemData::maxContents stacks nesting at most QcItemData::maxDepth levels and holding at most
// QcItemData::maxStacksInside stacks, names of at most QcItemData::maxNameLength characters;
// anything else turns the stream corrupt.
void writeStack(QDataStream &stream, const QcItemStack &stack);
QcItemStack readStack(QDataStream &stream);
// A stack from a client: as readStack, but contents turn the stream corrupt (a client never sends
// stacks inside a stack).
QcItemStack readFlatStack(QDataStream &stream);
void writeStacks(QDataStream &stream, const QList<QcItemStack> &stacks);
// At most 1024 stacks; std::nullopt for a larger count.
[[nodiscard]] std::optional<QList<QcItemStack>> readStacks(QDataStream &stream);
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
void encode(QDataStream &stream, QcMsg::Ping m);
void encode(QDataStream &stream, QcMsg::Pong m);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::Ping>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::Pong>, QDataStream &stream);
void encode(QDataStream &stream, QcMsg::PauseRequest m);
void encode(QDataStream &stream, QcMsg::ServerPause m);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::PauseRequest>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ServerPause>, QDataStream &stream);
void encode(QDataStream &stream, QcMsg::LatencyProbe m);
void encode(QDataStream &stream, QcMsg::LatencyEcho m);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::LatencyProbe>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::LatencyEcho>, QDataStream &stream);

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
void encode(QDataStream &stream, QcMsg::SpawnerDisplay m);
void encode(QDataStream &stream, const QcMsg::BlockBatch &m);
void encode(QDataStream &stream, const QcMsg::Explosion &m);
void encode(QDataStream &stream, const QcMsg::CampfireDisplay &m);
void encode(QDataStream &stream, QcMsg::BlockAck m);
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
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::SpawnerDisplay>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::BlockBatch>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::Explosion>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CampfireDisplay>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::BlockAck>, QDataStream &stream);

// Signs (QcSignCodec.cpp)
void encode(QDataStream &stream, const QcMsg::SignText &m);
void encode(QDataStream &stream, const QcMsg::OpenSignEditor &m);
void encode(QDataStream &stream, const QcMsg::EditSign &m);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::SignText>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::OpenSignEditor>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::EditSign>, QDataStream &stream);

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
void encode(QDataStream &stream, QcMsg::PlayerExperience m);
void encode(QDataStream &stream, const QcMsg::GameMode &m);
void encode(QDataStream &stream, const QcMsg::CreativeItem &m);
void encode(QDataStream &stream, QcMsg::PickBlock m);
void encode(QDataStream &stream, QcMsg::DrawLauncher m);
void encode(QDataStream &stream, QcMsg::ThrowItem m);
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
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::PlayerExperience>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::GameMode>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CreativeItem>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::PickBlock>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::DrawLauncher>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ThrowItem>, QDataStream &stream);

// Trading (QcTradeCodec.cpp)
void encode(QDataStream &stream, const QcMsg::TradeState &m);
void encode(QDataStream &stream, QcMsg::Trade m);
void encode(QDataStream &stream, QcMsg::CloseTrade m);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::TradeState>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::Trade>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CloseTrade>, QDataStream &stream);

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
void encode(QDataStream &stream, QcMsg::OpenStonecutter m);
void encode(QDataStream &stream, const QcMsg::StonecutterState &m);
void encode(QDataStream &stream, const QcMsg::SelectStonecutterRecipe &m);
void encode(QDataStream &stream, QcMsg::CloseStonecutter m);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::OpenStonecutter>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::StonecutterState>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::SelectStonecutterRecipe>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CloseStonecutter>, QDataStream &stream);
void encode(QDataStream &stream, QcMsg::OpenEnchanting m);
void encode(QDataStream &stream, const QcMsg::EnchantingState &m);
void encode(QDataStream &stream, QcMsg::ChooseEnchantment m);
void encode(QDataStream &stream, QcMsg::CloseEnchanting m);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::OpenEnchanting>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::EnchantingState>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ChooseEnchantment>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CloseEnchanting>, QDataStream &stream);
void encode(QDataStream &stream, QcMsg::OpenAnvil m);
void encode(QDataStream &stream, const QcMsg::AnvilState &m);
void encode(QDataStream &stream, const QcMsg::RenameAnvil &m);
void encode(QDataStream &stream, const QcMsg::ForgeAnvil &m);
void encode(QDataStream &stream, QcMsg::CloseAnvil m);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::OpenAnvil>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AnvilState>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::RenameAnvil>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::ForgeAnvil>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::CloseAnvil>, QDataStream &stream);

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

// Maps (QcMapCodec.cpp)
void encode(QDataStream &stream, const QcMsg::MapUpdate &m);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::MapUpdate>, QDataStream &stream);

// Admin channel (QcAdminCodec.cpp)
void encode(QDataStream &stream, QcMsg::AdminSubscribe m);
void encode(QDataStream &stream, const QcMsg::AdminRequest &m);
void encode(QDataStream &stream, const QcMsg::AdminResult &m);
void encode(QDataStream &stream, const QcMsg::AdminStatus &m);
void encode(QDataStream &stream, const QcMsg::AdminHistory &m);
void encode(QDataStream &stream, const QcMsg::AdminPlayers &m);
void encode(QDataStream &stream, const QcMsg::AdminChat &m);
void encode(QDataStream &stream, const QcMsg::AdminCatalogue &m);
void encode(QDataStream &stream, const QcMsg::AdminLists &m);
void encode(QDataStream &stream, const QcMsg::AdminSettings &m);
void encode(QDataStream &stream, const QcMsg::AdminLog &m);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminSubscribe>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminRequest>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminResult>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminStatus>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminHistory>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminPlayers>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminChat>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminCatalogue>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminLists>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminSettings>, QDataStream &stream);
[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::AdminLog>, QDataStream &stream);

} // namespace QcProtocolCodec
