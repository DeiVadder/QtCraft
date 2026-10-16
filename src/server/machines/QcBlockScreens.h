#pragma once

#include "QcBlockEntities.h"
#include "QcBlockPos.h"

struct QcMinecart;
struct QcServerClient;
class QcServerState;

// The screens of containers and machines the players have open (chests, hoppers, dispensers,
// furnaces, container minecarts): opening, closing, sending their state.
class QcBlockScreens
{
public:
    // The slots of an open chest (a block at `pos`) or container minecart (`cart`, decision E-386).
    struct OpenContainer
    {
        QList<QcItemStack> *stacks = nullptr;
        QcBlockPos pos;
        quint32 cart = 0;
    };

    explicit QcBlockScreens(QcServerState &state);

    // Opens the machine or chest screen of the block at `pos` for the player.
    void open(QcServerClient &client, QcBlockPos pos);
    // Opens the screen of a container minecart for the player.
    void openCart(QcServerClient &client, const QcMinecart &cart);
    // Forgets the open machine; `notify` tells the client (the server closed it).
    void close(QcServerClient &client, bool notify);
    // The open machine of the player while it still exists, else null.
    QcBlockEntities::Machine *openMachineOf(const QcServerClient &client);
    // The open chest or container cart of the player while it still exists, else nothing.
    [[nodiscard]] std::optional<OpenContainer> openContainerOf(const QcServerClient &client);
    // The slots of the container changed: everyone looking into it sees them, comparators and
    // hoppers around hear of it when `changed`.
    void containerChanged(const OpenContainer &container, bool changed);
    // Sends the slots of the cart to every player looking into it.
    void sendToCartViewers(quint32 cart);
    // Sends the open machine's state if it changed since the last message (or always with `force`).
    void send(QcServerClient &client, bool force = false);
    // Sends the state to every player looking into the machine or chest at `pos`.
    void sendToViewers(QcBlockPos pos);
    // Once per tick: sends what changed, closes screens whose block is gone, whose chunk unloaded or
    // whose player died or walked away.
    void update();

private:
    void sendContainer(QcServerClient &client, const OpenContainer &container, bool force);
    // Whether the player can still reach what the screen shows.
    bool canReach(const QcServerClient &client) const;
    // A block's or cart's screen opening closes the player's trading screen (one screen at a time).
    void endTrade(QcServerClient &client);

    QcServerState &m_state;
};
