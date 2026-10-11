#pragma once

#include "QcConnection.h"
#include "QcTestLogin.h"
#include "QcTransport.h"

#include <QHash>
#include <QSet>

#include <optional>

// A raw protocol client: sends exactly what a test wants and records everything the server says.
// It answers a login challenge like the real client (decision E-252) with `key`, `password` and
// `packs` (by default a key made from the name, no password and the server's own packs).
class QcRawClient
{
public:
    explicit QcRawClient(std::unique_ptr<QcTransport> transport)
        : connection(std::move(transport), QcProtocol::Peer::Server, [this](QcMessage &&message) { received(std::move(message)); })
    {
    }

    // Sends Hello; without a key of its own the client uses one made from the name, so the same
    // name logs in with the same identity in every test client.
    void hello(const QString &name, const QcAppearance &look = {}, quint32 version = QcProtocol::version)
    {
        if (key.isEmpty()) {
            key = QcTestLogin::keyFor(name);
        }
        connection.send(QcMsg::Hello{version, name, look});
    }

    QcMsg::LoginResponse answer(const QcMsg::LoginChallenge &challenge) const
    {
        return QcTestLogin::answer(challenge, {key, password, packs, viewDistance});
    }

    void received(QcMessage &&message)
    {
        ++messageCount;
        if (const auto *challenge = std::get_if<QcMsg::LoginChallenge>(&message)) {
            this->challenge = *challenge;
            if (answerChallenge) {
                connection.send(answer(*challenge));
            }
        } else if (const auto *players = std::get_if<QcMsg::PlayerList>(&message)) {
            playerList = players->players;
            ++playerListMessages;
        } else if (const auto *state = std::get_if<QcMsg::PlayerState>(&message)) {
            motion = state->motion;
            acknowledged = state->lastInputSequence;
        } else if (const auto *welcome = std::get_if<QcMsg::Welcome>(&message)) {
            entityId = welcome->playerEntityId;
            viewDistanceGranted = welcome->viewDistance;
        } else if (const auto *sound = std::get_if<QcMsg::SoundEvent>(&message)) {
            sounds.append(sound->key);
        } else if (const auto *inventory = std::get_if<QcMsg::InventoryState>(&message)) {
            stacks = inventory->stacks;
            craftingGrid = inventory->craftingGrid;
            craftingSize = inventory->craftingSize;
            craftingResult = inventory->craftingResult;
            equipment = inventory->equipment;
        } else if (const auto *update = std::get_if<QcMsg::BlockUpdate>(&message)) {
            blockUpdates.append(*update);
        } else if (const auto *chat = std::get_if<QcMsg::ChatMessage>(&message)) {
            chatLines.append(chat->text);
            chatKeys.append(*chat);
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
        } else if (const auto *disconnect = std::get_if<QcMsg::Disconnect>(&message)) {
            disconnectReason = disconnect->reason;
            disconnectKey = disconnect->key;
        } else {
            receivedWorld(message);
        }
    }

    // Entities, labels and the world time.
    void receivedWorld(const QcMessage &message)
    {
        if (const auto *spawn = std::get_if<QcMsg::EntitySpawn>(&message)) {
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
        } else if (const auto *debug = std::get_if<QcMsg::ServerDebug>(&message)) {
            debugMessages.append(*debug);
        } else if (const auto *chunk = std::get_if<QcMsg::ChunkData>(&message)) {
            chunks.append(chunk->chunk->pos());
        } else if (const auto *labels = std::get_if<QcMsg::ChunkLabels>(&message)) {
            chunkLabels.insert(labels->pos, labels->labels);
        } else if (const auto *time = std::get_if<QcMsg::WorldTime>(&message)) {
            ++timeMessages;
            worldTime = time->time;
            timeRunning = time->running;
            timeSpeed = time->speed;
        } else if (const auto *moves = std::get_if<QcMsg::EntityMoves>(&message)) {
            ++entityMoveMessages;
            for (const QcMsg::EntityMove &move : moves->moves) {
                flags[move.id] |= move.flags;
                if (move.hurt) {
                    hurt.insert(move.id);
                }
            }
        }
    }

    QcConnection connection;
    // The login: the installation key (set by hello() when empty), the server password, the packs
    // to report (none: the server's), the wanted view distance, and whether to answer at all.
    QByteArray key;
    QString password;
    std::optional<QList<QcPackFingerprint>> packs;
    qint32 viewDistance = 0;
    bool answerChallenge = true;
    std::optional<QcMsg::LoginChallenge> challenge;
    QList<QcMsg::PlayerList::Entry> playerList;
    int playerListMessages = 0;
    // The player's own entity id and view distance (from Welcome).
    quint32 entityId = 0;
    qint32 viewDistanceGranted = -1;
    QcEntityMotion motion;
    // The latest input the server acknowledged (PlayerState).
    quint32 acknowledged = 0;
    // Messages received in all, and the WorldTime and EntityMoves among them.
    int messageCount = 0;
    int timeMessages = 0;
    int entityMoveMessages = 0;
    // Keys of all sounds heard, oldest first.
    QStringList sounds;
    QList<QcItemStack> stacks;
    QList<QcItemStack> craftingGrid;
    qint32 craftingSize = 0;
    QcItemStack craftingResult;
    QcEquipment equipment;
    qint32 openedCrafting = 0;
    QList<QcMsg::BlockUpdate> blockUpdates;
    // Positions of the chunks received, in order.
    QList<QcChunkPos> chunks;
    QStringList chatLines;
    // Why the server closed the connection (English text and translation key); empty while connected.
    QString disconnectReason;
    QString disconnectKey;
    // The same lines with their translation keys and arguments.
    QList<QcMsg::ChatMessage> chatKeys;
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
    // Labels per chunk and the last world time.
    QHash<QcChunkPos, QList<QcWorldLabel>> chunkLabels;
    double worldTime = -1.0;
    bool timeRunning = true;
    // Server ticks per client tick for the time of day (decision E-249).
    double timeSpeed = 1.0;
    // Debug data received after a DebugSubscribe, oldest first.
    QList<QcMsg::ServerDebug> debugMessages;
};
