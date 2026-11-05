// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "common/gameplay/QcAnvil.h"
#include "common/protocol/QcMessages.h"

class QcBlockScreens;
class QcServerBlocks;
class QcServerInventory;
class QcServerSounds;
struct QcServerClient;
class QcServerState;

/*! \brief The anvil's screen in the server (decision E-866), as in the reference game: using an
    anvil opens it; the player puts a stack into the left slot (machineInputSlot) and optionally
    material, a second item of the kind or nothing into the right slot (machineFuelSlot), types a
    name and takes the output (QcAnvil::result) for its levels (nothing in creative mode).

    Taking it uses up the left stack and what the right one gave, and wears the anvil one step
    with QcAnvil::wearChance (QcBlockDefinition::wearsInto; the last step breaks it). The screen
    closes when the anvil is gone, out of reach, the player died or became a spectator; the slots
    then go back. */
class QcServerAnvil
{
public:
    QcServerAnvil(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory, QcBlockScreens &screens, QcServerBlocks &blocks);
    /*! The closer registered with QcBlockScreens refers to this object. */
    QcServerAnvil(const QcServerAnvil &) = delete;
    QcServerAnvil &operator=(const QcServerAnvil &) = delete;
    ~QcServerAnvil() = default;

    /*! Opens the screen of the anvil at \a pos for \a client. Returns \c false when it is no anvil.
        Any other screen closes; the same anvil again keeps its slots. */
    bool open(QcServerClient &client, QcBlockPos pos);
    /*! Sets the text of the name field of \a client to that of \a request; the client gets the
        new AnvilState (not the inventory). */
    void rename(QcServerClient &client, const QcMsg::RenameAnvil &request);
    /*! Takes the output for \a client when it is still the output and cost of \a request the
        client saw and the player can pay. */
    void forge(QcServerClient &client, const QcMsg::ForgeAnvil &request);
    /*! Gives the slots back to \a client and forgets the screen; \a notify tells the client (the
        server closed it). */
    void close(QcServerClient &client, bool notify);
    /*! Once per tick: closes the screens that may not stay open. */
    void update();

    /*! Returns what the anvil \a client has open in \a state makes now; empty without an open
        anvil. */
    [[nodiscard]] static QcAnvil::Result resultFor(const QcServerState &state, const QcServerClient &client);
    /*! Returns the state message for the anvil \a client has open in \a state;
        QcServerInventory::send sends it with every inventory state while the anvil is open. */
    [[nodiscard]] static QcMsg::AnvilState stateOf(const QcServerState &state, const QcServerClient &client);
    /*! Wears the anvil at \a pos of \a state one step: into QcBlockDefinition::wearsInto (set
        with \a blocks), or it breaks with its sound from \a sounds. */
    static void wear(QcServerState &state, QcServerBlocks &blocks, QcServerSounds &sounds, QcBlockPos pos);

private:
    // Whether the player's anvil still stands within reach and the player lives and acts.
    bool canStayOpen(const QcServerClient &client) const;
    // Whether the block at `pos` is an anvil.
    static bool isAnvil(const QcServerState &state, QcBlockPos pos);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
    QcBlockScreens &m_screens;
    QcServerBlocks &m_blocks;
};
