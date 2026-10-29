// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "common/protocol/QcMessages.h"
#include "server/blockentities/QcBlockEntities.h"
#include "server/machines/QcContainerSounds.h"

#include <functional>

class QcServerBlocks;
class QcServerSounds;
struct QcMinecart;
struct QcServerClient;
class QcServerState;

// The screens of containers and machines the players have open (chests, hoppers, dispensers,
// furnaces, container minecarts): opening, closing, sending their state. A container whose signal
// counts its viewers (the trapped chest, decision E-783) shows in its state `power` how many players
// look into it, at most 15. Storage blocks sound as they open and close (QcContainerSounds).
class QcBlockScreens
{
public:
    // The slots of an open chest (a block at `pos`), container minecart (`cart`, decision E-386) or
    // backpack opened from the hand (`heldSlot`, decision E-833). A portable container (a backpack,
    // placed or held) holds nothing with contents (QcPortableContainers).
    struct OpenContainer
    {
        QList<QcItemStack> *stacks = nullptr;
        QcBlockPos pos;
        quint32 cart = 0;
        bool portable = false;
        int heldSlot = QcMsg::OpenMachine::noHeldSlot;
    };

    QcBlockScreens(QcServerState &state, QcServerBlocks &blocks, QcServerSounds &sounds);

    // Opens the machine or chest screen of the block at `pos` for the player.
    void open(QcServerClient &client, QcBlockPos pos);
    // Opens the screen of a container minecart for the player.
    void openCart(QcServerClient &client, const QcMinecart &cart);
    // Opens the held backpack (a portable container in the selected hotbar slot, decision E-833):
    // its slots and the hotbar, its own slot locked. Contents its slots cannot show (beyond the slot
    // count, or refused there) leave the item and are returned for the caller to give to the
    // player, so the first click into the screen cannot delete them. Nothing when the player holds
    // no backpack.
    [[nodiscard]] std::optional<QList<QcItemStack>> openHeld(QcServerClient &client);
    // Forgets the open machine; `notify` tells the client (the server closed it).
    void close(QcServerClient &client, bool notify);
    // The open machine of the player while it still exists, else null.
    QcBlockEntities::Machine *openMachineOf(const QcServerClient &client);
    // The open chest, container cart or held backpack of the player while it still exists, else nothing.
    [[nodiscard]] std::optional<OpenContainer> openContainerOf(QcServerClient &client);
    // The slots of the container `client` has open changed: everyone looking into it sees them,
    // comparators and hoppers around hear of it when `changed`. A held backpack has no place in the
    // world: only its holder sees it.
    void containerChanged(QcServerClient &client, const OpenContainer &container, bool changed);
    // Sends the slots of the cart to every player looking into it.
    void sendToCartViewers(quint32 cart);
    // Sends the open machine's state if it changed since the last message (or always with `force`).
    void send(QcServerClient &client, bool force = false);
    // Sends the state to every player looking into the machine or chest at `pos`.
    void sendToViewers(QcBlockPos pos);
    // Closes the player's open stonecutter (with notice; its input goes back), if any.
    void closeStonecutter(QcServerClient &client);
    // Closes the player's open enchanting table (with notice; its slots go back), if any.
    void closeEnchanting(QcServerClient &client);
    // Closes the player's open anvil (with notice; its slots go back), if any.
    void closeAnvil(QcServerClient &client);
    // Ends the player's trading screen (with notice), if any; QcServerTrading::update releases the
    // villager.
    void endTrade(QcServerClient &client);

    // How an open stonecutter is closed (QcServerStonecutter::close with notice, which gives its
    // input back) when a block's, cart's or villager's screen opens; set by QcServerStonecutter.
    void setStonecutterCloser(std::function<void(QcServerClient &)> closer);
    // The same for an open enchanting table; set by QcServerEnchanting (decision E-864).
    void setEnchantingCloser(std::function<void(QcServerClient &)> closer);
    // The same for an open anvil; set by QcServerAnvil (decision E-866).
    void setAnvilCloser(std::function<void(QcServerClient &)> closer);

    // Once per tick: sends what changed, closes screens whose block is gone, whose chunk unloaded or
    // whose player died or walked away; containers that count their viewers take the new count, and
    // containers opened or closed since the last tick sound.
    void update();
    // Nobody looks into the containers of a chunk that just arrived: those that count their viewers
    // start at 0 whatever was saved.
    static void releaseViewers(QcServerState &state, QcChunkPos chunk);

private:
    void sendContainer(QcServerClient &client, const OpenContainer &container, bool force);
    // The held backpack's container while the player still holds it, else nothing.
    [[nodiscard]] std::optional<OpenContainer> heldContainerOf(QcServerClient &client);
    // Whether a block, cart or held backpack screen is open.
    static bool hasScreen(const QcServerClient &client);
    // Whether the player can still reach what the screen shows.
    bool canReach(const QcServerClient &client) const;
    // A block's or cart's screen opening closes the player's trading screen and stonecutter (one
    // screen at a time).
    void closeOtherScreens(QcServerClient &client);
    // Sets the viewer counts of the containers that count them (QcSignalKind::Viewers).
    void updateViewers();

    QcServerState &m_state;
    QcServerBlocks &m_blocks;
    // The containers counting their viewers that had any at the latest update.
    QHash<QcBlockPos, int> m_viewers;
    QcContainerSounds m_containerSounds;
    std::function<void(QcServerClient &)> m_closeStonecutter;
    std::function<void(QcServerClient &)> m_closeEnchanting;
    std::function<void(QcServerClient &)> m_closeAnvil;
};
