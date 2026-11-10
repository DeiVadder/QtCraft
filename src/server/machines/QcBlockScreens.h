// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "common/protocol/QcMessages.h"
#include "server/blockentities/QcBlockEntities.h"
#include "server/dimensions/QcPerDimension.h"
#include "server/machines/QcContainerSounds.h"

#include <array>
#include <functional>
#include <optional>

class QcServerBlocks;
class QcServerSounds;
struct QcMinecart;
struct QcServerClient;
class QcServerState;

/*! The block screens with slots of their own that QcServerWorkstations runs: the stonecutter
    (decision E-785), the enchanting table (E-864), the anvil (E-866), the smithing table
    (E-1115) and the beacon (E-1142). */
enum class QcWorkstation : quint8 {
    /*! The stonecutter. */
    Stonecutter,
    /*! The enchanting table. */
    Enchanting,
    /*! The anvil. */
    Anvil,
    /*! The smithing table. */
    Smithing,
    /*! The beacon (decision E-1142). */
    Beacon,
};

/*! \brief The screens of containers and machines the players have open (chests, hoppers,
    dispensers, furnaces, container minecarts): opening, closing, sending their state.

    A container whose signal counts its viewers (the trapped chest, decision E-783) shows in its
    state \c {power} how many players look into it, at most 15. Storage blocks sound as they open
    and close (QcContainerSounds). */
class QcBlockScreens
{
public:
    /*! \brief The slots of an open chest (a block at \c {pos}), container minecart (\c {cart},
        decision E-386) or backpack opened from the hand (\c {heldSlot}, decision E-833).

        A portable container (a backpack, placed or held) holds nothing with contents
        (QcPortableContainers). \c {stacks} points to its slots, \c {portable} marks a portable
        container. \qcfields */
    struct OpenContainer
    {
        QList<QcItemStack> *stacks = nullptr;
        QcBlockPos pos;
        quint32 cart = 0;
        bool portable = false;
        int heldSlot = QcMsg::OpenMachine::noHeldSlot;
    };

    QcBlockScreens(QcServerState &state, QcServerBlocks &blocks, QcServerSounds &sounds);

    /*! Opens the machine or chest screen of the block at \a pos for the player \a client. */
    void open(QcServerClient &client, QcBlockPos pos);
    /*! Opens the screen of the container minecart \a cart for the player \a client. */
    void openCart(QcServerClient &client, const QcMinecart &cart);
    /*! Opens the backpack \a client holds (a portable container in the selected hotbar slot,
        decision E-833): its slots and the hotbar, its own slot locked. Contents its slots cannot
        show (beyond the slot count, or refused there) leave the item and are returned for the
        caller to give to the player, so the first click into the screen cannot delete them.
        Returns those contents, or nothing when the player holds no backpack. */
    [[nodiscard]] std::optional<QList<QcItemStack>> openHeld(QcServerClient &client);
    /*! Forgets the open machine of \a client; \a notify tells the client (the server closed it). */
    void close(QcServerClient &client, bool notify);
    /*! \a client travels: every screen he has open ends and he is told, the workstations give
        their slots back, a sign he was writing stays as it is. */
    void closeAll(QcServerClient &client);
    /*! Returns the open machine of the player \a client while it still exists, else null. */
    QcBlockEntities::Machine *openMachineOf(const QcServerClient &client);
    /*! Returns the open chest, container cart or held backpack of the player \a client while it
        still exists, else nothing. */
    [[nodiscard]] std::optional<OpenContainer> openContainerOf(QcServerClient &client);
    /*! The slots of the \a container \a client has open changed: everyone looking into it sees
        them, comparators and hoppers around hear of it when \a changed. A held backpack has no
        place in the world: only its holder sees it. */
    void containerChanged(QcServerClient &client, const OpenContainer &container, bool changed);
    /*! Sends the slots of the minecart \a cart to every player looking into it. */
    void sendToCartViewers(quint32 cart);
    /*! Sends the state of the machine \a client has open if it changed since the last message
        (or always with \a force). */
    void send(QcServerClient &client, bool force = false);
    /*! Sends the state to every player looking into the machine or chest at \a pos. */
    void sendToViewers(QcBlockPos pos);
    /*! Closes the open workstation screen of \a client (with notice; its slots go back), if any,
        unless it is of the kind \a keep. */
    void closeWorkstationsExcept(QcServerClient &client, std::optional<QcWorkstation> keep = std::nullopt);
    /*! Ends the trading screen of \a client (with notice), if any; QcServerTrading::update releases
        the villager. */
    void endTrade(QcServerClient &client);

    /*! Sets the \a closer of the workstation screens of the \a kind: how one is closed (its
        close with notice, which gives its slots back and does nothing when none is open) when
        a block's, cart's, villager's or another workstation's screen opens; set by the
        workstation. */
    void setWorkstationCloser(QcWorkstation kind, std::function<void(QcServerClient &)> closer);

    /*! Once per tick: sends what changed, closes screens whose block is gone, whose chunk
        unloaded or whose player died or walked away; containers that count their viewers take
        the new count, and containers opened or closed since the last tick sound. */
    void update();
    /*! Nobody looks into the containers of \a chunk of \a state, which just arrived: those that
        count their viewers start at 0 whatever was saved. */
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
    QcContainerSounds m_containerSounds;
    std::array<std::function<void(QcServerClient &)>, 5> m_workstationClosers;
    // Who looks into the blocks of a dimension.
    struct Viewers
    {
        // The containers counting their viewers that had any at the latest update.
        QHash<QcBlockPos, int> viewers;
    };
    // Of the current dimension.
    Viewers &here() const;

    QcPerDimension<Viewers> m_perDimension;
};
