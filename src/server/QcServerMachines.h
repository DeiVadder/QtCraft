#pragma once

#include "QcBlockPos.h"
#include "QcContainerManager.h"
#include "QcMachineManager.h"

class QcServerBlocks;
class QcServerSounds;
struct QcServerClient;
struct QcServerState;

// Machine and chest screens of the players: opening, closing, sending their state, and the tick of
// the machines.
class QcServerMachines
{
public:
    // A burning machine crackles on average this often (seconds).
    static constexpr double machineSoundSeconds = 2.5;

    QcServerMachines(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks);

    // Opens the machine or chest screen of the block at `pos` for the player.
    void open(QcServerClient &client, QcBlockPos pos);
    // Forgets the open machine; `notify` tells the client (the server closed it).
    void close(QcServerClient &client, bool notify);
    // The open machine of the player while it still exists, else null.
    QcMachineManager::Machine *openMachineOf(const QcServerClient &client);
    // The open chest of the player while it still exists, else null.
    QcContainerManager::Container *openContainerOf(const QcServerClient &client);
    // Sends the open machine's state if it changed since the last message (or always with `force`).
    void send(QcServerClient &client, bool force = false);
    // Sends the state to every player looking into the machine or chest at `pos`.
    void sendToViewers(QcBlockPos pos);
    void tick();

private:
    void sendContainer(QcServerClient &client, const QcContainerManager::Container &container, bool force);
    void playMachineSounds();

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerBlocks &m_blocks;
};
