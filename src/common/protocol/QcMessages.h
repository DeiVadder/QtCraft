#pragma once

// Protocol messages. Plain data structs in thematic headers; together they form the QcMessage
// variant whose alternative index is the wire type tag. New messages are appended at the end.

#include "QcEntityMessages.h"
#include "QcMachineMessages.h"
#include "QcPlayerMessages.h"
#include "QcSessionMessages.h"
#include "QcWorldMessages.h"

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
                               QcMsg::SoundEvent, QcMsg::UseEntity, QcMsg::QuickMoveItem, QcMsg::ChunkLabels>;
