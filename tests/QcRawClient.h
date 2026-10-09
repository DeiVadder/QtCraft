#pragma once

#include "QcConnection.h"

#include <QHash>
#include <QSet>

#include <optional>

// A raw protocol client: sends exactly what a test wants and records everything the server says.
class QcRawClient
{
public:
    explicit QcRawClient(std::unique_ptr<QcTransport> transport)
        : connection(std::move(transport), QcProtocol::Peer::Server, [this](QcMessage &&message) { received(std::move(message)); })
    {
    }

    void received(QcMessage &&message)
    {
        if (const auto *state = std::get_if<QcMsg::PlayerState>(&message)) {
            motion = state->motion;
        } else if (const auto *inventory = std::get_if<QcMsg::InventoryState>(&message)) {
            stacks = inventory->stacks;
            craftingGrid = inventory->craftingGrid;
            craftingSize = inventory->craftingSize;
            craftingResult = inventory->craftingResult;
        } else if (const auto *chat = std::get_if<QcMsg::ChatMessage>(&message)) {
            chatLines.append(chat->text);
        } else if (const auto *health = std::get_if<QcMsg::PlayerHealth>(&message)) {
            this->health = health->health;
            dead = health->dead;
        } else if (const auto *open = std::get_if<QcMsg::OpenCrafting>(&message)) {
            openedCrafting = open->size;
        } else if (const auto *food = std::get_if<QcMsg::PlayerFood>(&message)) {
            this->food = food->food;
        } else if (const auto *machine = std::get_if<QcMsg::OpenMachine>(&message)) {
            openMachine = machine->pos;
            machineCategory = machine->category;
            machineClosed = false;
        } else if (const auto *machine = std::get_if<QcMsg::MachineState>(&message)) {
            machineStacks = machine->stacks;
            machineBurn = machine->burn;
        } else if (std::holds_alternative<QcMsg::CloseMachine>(message)) {
            openMachine.reset();
            machineClosed = true;
        } else if (const auto *spawn = std::get_if<QcMsg::EntitySpawn>(&message)) {
            entities.insert(spawn->id, spawn->mobType);
            if (spawn->mobType == QcMsg::EntitySpawn::itemType) {
                items.insert(spawn->id, spawn->item);
            }
            spawns.insert(spawn->id, *spawn);
        } else if (const auto *removed = std::get_if<QcMsg::EntityRemove>(&message)) {
            entities.remove(removed->id);
            items.remove(removed->id);
            if (removed->died) {
                died.insert(removed->id);
            }
        } else if (const auto *moves = std::get_if<QcMsg::EntityMoves>(&message)) {
            for (const QcMsg::EntityMove &move : moves->moves) {
                flags[move.id] |= move.flags;
                if (move.hurt) {
                    hurt.insert(move.id);
                }
            }
        }
    }

    QcConnection connection;
    QcEntityMotion motion;
    QList<QcItemStack> stacks;
    QList<QcItemStack> craftingGrid;
    qint32 craftingSize = 0;
    QcItemStack craftingResult;
    qint32 openedCrafting = 0;
    QStringList chatLines;
    double health = 0.0;
    bool dead = false;
    int food = -1;
    std::optional<QcBlockPos> openMachine;
    QString machineCategory;
    QList<QcItemStack> machineStacks;
    float machineBurn = 0.0f;
    // The server closed the machine screen.
    bool machineClosed = false;
    QHash<quint32, qint32> entities;
    // Item entities the client was told about, with their latest stack.
    QHash<quint32, QcItemStack> items;
    // Latest spawn message per entity, entities reported dead, and all move flags seen per entity.
    QHash<quint32, QcMsg::EntitySpawn> spawns;
    QSet<quint32> died;
    QHash<quint32, quint8> flags;
    // Entities reported hurt in any move.
    QSet<quint32> hurt;
};
