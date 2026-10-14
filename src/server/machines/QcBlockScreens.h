#pragma once

#include "QcBlockEntities.h"
#include "QcBlockPos.h"

struct QcServerClient;
class QcServerState;

// The screens of containers and machines the players have open (chests, hoppers, dispensers,
// furnaces): opening, closing, sending their state.
class QcBlockScreens
{
public:
    explicit QcBlockScreens(QcServerState &state);

    // Opens the machine or chest screen of the block at `pos` for the player.
    void open(QcServerClient &client, QcBlockPos pos);
    // Forgets the open machine; `notify` tells the client (the server closed it).
    void close(QcServerClient &client, bool notify);
    // The open machine of the player while it still exists, else null.
    QcBlockEntities::Machine *openMachineOf(const QcServerClient &client);
    // The open chest of the player while it still exists, else null.
    QcBlockEntities::Container *openContainerOf(const QcServerClient &client);
    // Sends the open machine's state if it changed since the last message (or always with `force`).
    void send(QcServerClient &client, bool force = false);
    // Sends the state to every player looking into the machine or chest at `pos`.
    void sendToViewers(QcBlockPos pos);
    // Once per tick: sends what changed, closes screens whose block is gone, whose chunk unloaded or
    // whose player died or walked away.
    void update();

private:
    void sendContainer(QcServerClient &client, const QcBlockEntities::Container &container, bool force);

    QcServerState &m_state;
};
