#pragma once

#include "common/protocol/QcConnection.h"
#include "common/protocol/QcTransport.h"
#include "QcTestLogin.h"

#include <QHash>
#include <QSet>

#include <functional>
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
            vehicle = state->vehicle;
        } else if (const auto *welcome = std::get_if<QcMsg::Welcome>(&message)) {
            entityId = welcome->playerEntityId;
            viewDistanceGranted = welcome->viewDistance;
        } else if (const auto *sound = std::get_if<QcMsg::SoundEvent>(&message)) {
            sounds.append(sound->key);
        } else if (const auto *inventory = std::get_if<QcMsg::InventoryState>(&message)) {
            stacks = inventory->stacks;
            selected = inventory->selected;
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
            ++healthMessages;
            this->health = health->health;
            dead = health->dead;
            burning = health->burning;
            effects = health->effects;
        } else if (const auto *open = std::get_if<QcMsg::OpenCrafting>(&message)) {
            openedCrafting = open->size;
        } else if (const auto *food = std::get_if<QcMsg::PlayerFood>(&message)) {
            this->food = food->food;
        } else if (const auto *experience = std::get_if<QcMsg::PlayerExperience>(&message)) {
            this->experience = *experience;
        } else if (const auto *mode = std::get_if<QcMsg::GameMode>(&message)) {
            gameMode = mode->mode;
        } else if (const auto *machine = std::get_if<QcMsg::OpenMachine>(&message)) {
            openMachine = machine->pos;
            openMachineEntity = machine->entity;
            machineCategory = machine->category;
            machineClosed = false;
        } else if (const auto *machine = std::get_if<QcMsg::MachineState>(&message)) {
            machineStacks = machine->stacks;
            machineBurn = machine->burn;
        } else if (std::holds_alternative<QcMsg::CloseMachine>(message)) {
            openMachine.reset();
            machineClosed = true;
        } else if (const auto *trading = std::get_if<QcMsg::TradeState>(&message)) {
            trade = *trading;
        } else if (std::holds_alternative<QcMsg::CloseTrade>(message)) {
            trade.reset();
            tradeClosed = true;
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
        const bool aboutEntities = std::holds_alternative<QcMsg::EntitySpawn>(message) || std::holds_alternative<QcMsg::EntityMoves>(message)
                                   || std::holds_alternative<QcMsg::EntityRemove>(message);
        if (onEntityMessage && aboutEntities) {
            onEntityMessage(message);
        }
        if (const auto *spawn = std::get_if<QcMsg::EntitySpawn>(&message)) {
            entities.insert(spawn->id, spawn->kind);
            if (spawn->kind == QcEntityKind::Item) {
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
        } else if (const auto *weather = std::get_if<QcMsg::Weather>(&message)) {
            ++weatherMessages;
            this->weather = *weather;
        } else if (const auto *strike = std::get_if<QcMsg::Lightning>(&message)) {
            lightning.append(strike->position);
        } else if (const auto *moves = std::get_if<QcMsg::EntityMoves>(&message)) {
            ++entityMoveMessages;
            for (const QcMsg::EntityMove &move : moves->moves) {
                flags[move.id] |= move.flags;
                held[move.id] = move.held;
                if (move.hurt) {
                    hurt.insert(move.id);
                }
            }
        }
    }

    QcConnection connection;
    // Called with every spawn, move and removal of an entity, in the order received (comparison runs).
    std::function<void(const QcMessage &)> onEntityMessage;
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
    // The latest input the server acknowledged and the minecart ridden (PlayerState).
    quint32 acknowledged = 0;
    quint32 vehicle = 0;
    // Messages received in all, and the WorldTime and EntityMoves among them.
    int messageCount = 0;
    int timeMessages = 0;
    int entityMoveMessages = 0;
    // Keys of all sounds heard, oldest first.
    QStringList sounds;
    QList<QcItemStack> stacks;
    qint32 selected = 0;
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
    // The player is on fire (decision E-393).
    bool burning = false;
    // His status effects (decision E-450) and how many health messages brought them.
    QList<QcStatusEffect> effects;
    int healthMessages = 0;
    int food = -1;
    // The experience the server reported last (decision E-398); empty before the first.
    std::optional<QcMsg::PlayerExperience> experience;
    // The game mode the server reported last (decision E-282); empty before the first.
    std::optional<QcGameMode> gameMode;
    std::optional<QcBlockPos> openMachine;
    // The open trading screen (decision E-414) and whether the server closed one.
    std::optional<QcMsg::TradeState> trade;
    bool tradeClosed = false;
    // The container minecart of the open screen, 0 for a block.
    quint32 openMachineEntity = 0;
    QString machineCategory;
    QList<QcItemStack> machineStacks;
    float machineBurn = 0.0f;
    // The server closed the machine screen.
    bool machineClosed = false;
    // The kind of each entity the client knows.
    QHash<quint32, QcEntityKind> entities;
    // Item entities the client was told about, with their latest stack.
    QHash<quint32, QcItemStack> items;
    // Latest spawn message per entity, entities reported dead, and all move flags seen per entity.
    QHash<quint32, QcMsg::EntitySpawn> spawns;
    QSet<quint32> died;
    QHash<quint32, quint16> flags;
    // The latest held item per entity.
    QHash<quint32, QcItemId> held;
    // Entities reported hurt in any move.
    QSet<quint32> hurt;
    // Labels per chunk and the last world time.
    QHash<QcChunkPos, QList<QcWorldLabel>> chunkLabels;
    double worldTime = -1.0;
    // The latest weather, how many Weather messages came and where lightning struck (decision E-395).
    std::optional<QcMsg::Weather> weather;
    int weatherMessages = 0;
    QList<QcVec3> lightning;
    bool timeRunning = true;
    // Server ticks per client tick for the time of day (decision E-249).
    double timeSpeed = 1.0;
    // Debug data received after a DebugSubscribe, oldest first.
    QList<QcMsg::ServerDebug> debugMessages;
};
