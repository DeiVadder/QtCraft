// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "common/protocol/QcMessages.h"

class QcBlockScreens;
class QcServerInventory;
class QcServerSounds;
struct QcServerClient;
class QcServerState;

/*! \brief The smithing table's screen in the server (decision E-1115), as in the reference game:
    using a smithing table opens it; the player puts a template, a base item and an addition into
    its three slots (machineInputSlot onwards) and takes the output (QcSmithing::output).

    Taking it turns the base into the output in its slot and uses up one template and one
    addition; it costs nothing and the table never wears. The screen closes when the table is
    gone, out of reach, the player died or became a spectator; the slots then go back. */
class QcServerSmithing
{
public:
    QcServerSmithing(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory, QcBlockScreens &screens);
    /*! The closer registered with QcBlockScreens refers to this object. */
    QcServerSmithing(const QcServerSmithing &) = delete;
    QcServerSmithing &operator=(const QcServerSmithing &) = delete;
    ~QcServerSmithing() = default;

    /*! Opens the screen of the smithing table at \a pos for \a client. Returns \c false when it is
        no smithing table. Any other screen closes; the same table again keeps its slots. */
    bool open(QcServerClient &client, QcBlockPos pos);
    /*! Takes the output for \a client when it is still the output of \a request the client saw. */
    void forge(QcServerClient &client, const QcMsg::ForgeSmithing &request);
    /*! Gives the slots back to \a client and forgets the screen; \a notify tells the client (the
        server closed it). */
    void close(QcServerClient &client, bool notify);
    /*! Once per tick: closes the screens that may not stay open. */
    void update();

    /*! Returns the state message for the smithing table \a client has open in \a state;
        QcServerInventory::send sends it with every inventory state while the table is open. */
    [[nodiscard]] static QcMsg::SmithingState stateOf(const QcServerState &state, const QcServerClient &client);

private:
    // Whether the player's table still stands within reach and the player lives and acts.
    bool canStayOpen(const QcServerClient &client) const;
    // Whether the block at `pos` is a smithing table.
    static bool isSmithingTable(const QcServerState &state, QcBlockPos pos);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
    QcBlockScreens &m_screens;
};
