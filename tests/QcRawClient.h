#pragma once

#include "common/protocol/QcConnection.h"
#include "common/protocol/QcTransport.h"
#include "QcRawWorldRecord.h"
#include "QcTestLogin.h"

#include <QHash>
#include <QSet>

#include <functional>
#include <optional>

// A raw protocol client: sends exactly what a test wants and records everything the server says
// (what it says about the world in QcRawWorldRecord).
// It answers a login challenge like the real client (decision E-252) with `key`, `password` and
// `packs` (by default a key made from the name, no password and the server's own packs).
class QcRawClient : public QcRawWorldRecord
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
        if (!receivedPlayer(message) && !receivedScreen(message) && !receivedBlocks(message)) {
            receivedWorld(message);
        }
    }

    // The login, the own player, chat, sounds and the end of the connection; false for another message.
    bool receivedPlayer(const QcMessage &message)
    {
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
            soundPitches.append(sound->pitch);
        } else if (const auto *chat = std::get_if<QcMsg::ChatMessage>(&message)) {
            chatLines.append(chat->text);
            chatKeys.append(*chat);
        } else if (const auto *health = std::get_if<QcMsg::PlayerHealth>(&message)) {
            ++healthMessages;
            this->health = health->health;
            dead = health->dead;
            burning = health->burning;
            effects = health->effects;
        } else if (const auto *food = std::get_if<QcMsg::PlayerFood>(&message)) {
            this->food = food->food;
        } else if (const auto *experience = std::get_if<QcMsg::PlayerExperience>(&message)) {
            this->experience = *experience;
        } else if (const auto *mode = std::get_if<QcMsg::GameMode>(&message)) {
            gameMode = mode->mode;
            adventure = mode->adventure;
        } else if (const auto *disconnect = std::get_if<QcMsg::Disconnect>(&message)) {
            disconnectReason = disconnect->reason;
            disconnectKey = disconnect->key;
        } else {
            return false;
        }
        return true;
    }

    // The inventory, crafting, machines and trading; false for another message.
    bool receivedScreen(const QcMessage &message)
    {
        if (const auto *inventory = std::get_if<QcMsg::InventoryState>(&message)) {
            stacks = inventory->stacks;
            selected = inventory->selected;
            craftingGrid = inventory->craftingGrid;
            craftingSize = inventory->craftingSize;
            craftingResult = inventory->craftingResult;
            equipment = inventory->equipment;
        } else if (const auto *open = std::get_if<QcMsg::OpenCrafting>(&message)) {
            openedCrafting = open->size;
        } else if (const auto *machine = std::get_if<QcMsg::OpenMachine>(&message)) {
            openMachine = machine->pos;
            openMachineEntity = machine->entity;
            openMachineHeldSlot = machine->heldSlot;
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
        } else if (const auto *cutter = std::get_if<QcMsg::OpenStonecutter>(&message)) {
            stonecutter = cutter->pos;
        } else if (const auto *cutter = std::get_if<QcMsg::StonecutterState>(&message)) {
            stonecutterState = *cutter;
        } else if (std::holds_alternative<QcMsg::CloseStonecutter>(message)) {
            stonecutter.reset();
            stonecutterState = {};
            stonecutterClosed = true;
        } else {
            return false;
        }
        return true;
    }

    // Changed blocks, explosions and signs; false for another message.
    bool receivedBlocks(const QcMessage &message)
    {
        if (const auto *update = std::get_if<QcMsg::BlockUpdate>(&message)) {
            blockUpdates.append(*update);
        } else if (const auto *batch = std::get_if<QcMsg::BlockBatch>(&message)) {
            blockUpdates.append(batch->updates);
            ++blockBatches;
        } else if (const auto *explosion = std::get_if<QcMsg::Explosion>(&message)) {
            explosions.append(*explosion);
        } else if (const auto *sign = std::get_if<QcMsg::SignText>(&message)) {
            signTexts.append(*sign);
        } else if (const auto *editor = std::get_if<QcMsg::OpenSignEditor>(&message)) {
            signEditors.append(*editor);
        } else if (const auto *campfire = std::get_if<QcMsg::CampfireDisplay>(&message)) {
            campfires.insert(campfire->pos, campfire->items);
        } else if (const auto *map = std::get_if<QcMsg::MapUpdate>(&message)) {
            mapUpdates.append(*map);
        } else {
            return false;
        }
        return true;
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
    // The latest input the server acknowledged and the minecart ridden (PlayerState).
    quint32 acknowledged = 0;
    quint32 vehicle = 0;
    // Messages received in all.
    int messageCount = 0;
    // Keys of all sounds heard, oldest first.
    QStringList sounds;
    // Their pitches (playback speeds), parallel to `sounds`.
    QList<float> soundPitches;
    QList<QcItemStack> stacks;
    qint32 selected = 0;
    QList<QcItemStack> craftingGrid;
    qint32 craftingSize = 0;
    QcItemStack craftingResult;
    QcEquipment equipment;
    qint32 openedCrafting = 0;
    // Block updates, single ones and those of batches (decision E-604), and how many batches came.
    QList<QcMsg::BlockUpdate> blockUpdates;
    int blockBatches = 0;
    // What lies on each campfire, as the server last said (decision E-772).
    QHash<QcBlockPos, QList<quint32>> campfires;
    QList<QcMsg::Explosion> explosions;
    // Sign texts and sign editors the server sent (decision E-766), oldest first.
    QList<QcMsg::SignText> signTexts;
    QList<QcMsg::OpenSignEditor> signEditors;
    // Map updates (decision E-802), oldest first.
    QList<QcMsg::MapUpdate> mapUpdates;
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
    // The adventure allow-lists that came with it (decision E-735).
    QcAdventureRules adventure;
    std::optional<QcBlockPos> openMachine;
    // The open trading screen (decision E-414) and whether the server closed one.
    std::optional<QcMsg::TradeState> trade;
    bool tradeClosed = false;
    // The container minecart of the open screen, 0 for a block.
    quint32 openMachineEntity = 0;
    // The hotbar slot of a backpack opened from the hand (decision E-833), -1 for a block or cart.
    int openMachineHeldSlot = -1;
    QString machineCategory;
    QList<QcItemStack> machineStacks;
    float machineBurn = 0.0f;
    // The server closed the machine screen.
    bool machineClosed = false;
    // The open stonecutter (decision E-785), its slots as last sent and whether the server closed one.
    std::optional<QcBlockPos> stonecutter;
    QcMsg::StonecutterState stonecutterState;
    bool stonecutterClosed = false;
};
