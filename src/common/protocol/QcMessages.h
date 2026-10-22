#pragma once

// Protocol messages. Plain data structs in thematic headers; together they form the QcMessage
// variant whose alternative index is the wire type tag. New messages are appended at the end.

#include "common/protocol/QcAdminMessages.h"
#include "common/protocol/QcDebugMessages.h"
#include "common/protocol/QcEntityMessages.h"
#include "common/protocol/QcLoginMessages.h"
#include "common/protocol/QcMachineMessages.h"
#include "common/protocol/QcMapMessages.h"
#include "common/protocol/QcPlayerMessages.h"
#include "common/protocol/QcSessionMessages.h"
#include "common/protocol/QcSignMessages.h"
#include "common/protocol/QcTradeMessages.h"
#include "common/protocol/QcWorldMessages.h"

#include <variant>

using QcMessage = std::variant<QcMsg::Hello, QcMsg::Welcome, QcMsg::ContentSync, QcMsg::ChunkData, QcMsg::ChunkUnload,
                               QcMsg::BlockUpdate, QcMsg::PlayerInput, QcMsg::PlayerState, QcMsg::Disconnect,
                               QcMsg::StartMining, QcMsg::CancelMining, QcMsg::FinishMining, QcMsg::UseItem,
                               QcMsg::SelectHotbarSlot, QcMsg::MoveInventoryItem, QcMsg::ChatInput,
                               QcMsg::InventoryState, QcMsg::ChatMessage, QcMsg::EntitySpawn, QcMsg::EntityMoves,
                               QcMsg::EntityRemove, QcMsg::PlayerHealth, QcMsg::WorldTime, QcMsg::AttackEntity,
                               QcMsg::Respawn, QcMsg::TakeCraftingResult, QcMsg::CraftRecipe, QcMsg::CloseCrafting,
                               QcMsg::ConsumeItem, QcMsg::OpenCrafting, QcMsg::DropItem, QcMsg::SetAppearance,
                               QcMsg::OpenMachine, QcMsg::MachineState, QcMsg::CloseMachine, QcMsg::PlayerFood,
                               QcMsg::SoundEvent, QcMsg::UseEntity, QcMsg::QuickMoveItem, QcMsg::ChunkLabels,
                               QcMsg::DebugSubscribe, QcMsg::ServerDebug, QcMsg::LoginChallenge,
                               QcMsg::LoginResponse, QcMsg::PlayerList, QcMsg::GameMode, QcMsg::CreativeItem,
                               QcMsg::PickBlock, QcMsg::Weather, QcMsg::Lightning,
                               QcMsg::PlayerExperience, QcMsg::TradeState, QcMsg::Trade, QcMsg::CloseTrade,
                               QcMsg::DrawLauncher, QcMsg::ThrowItem, QcMsg::SpawnerDisplay, QcMsg::BlockBatch,
                               QcMsg::Explosion, QcMsg::CampfireDisplay, QcMsg::SignText, QcMsg::OpenSignEditor, QcMsg::EditSign,
                               QcMsg::OpenStonecutter, QcMsg::StonecutterState, QcMsg::SelectStonecutterRecipe,
                               QcMsg::CloseStonecutter, QcMsg::MapUpdate, QcMsg::Ping, QcMsg::Pong,
                               QcMsg::AdminSubscribe, QcMsg::AdminRequest, QcMsg::AdminResult, QcMsg::AdminStatus,
                               QcMsg::AdminHistory, QcMsg::AdminPlayers, QcMsg::AdminChat, QcMsg::AdminCatalogue,
                               QcMsg::AdminLists, QcMsg::AdminSettings, QcMsg::AdminLog>;
