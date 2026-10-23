#pragma once

#include "common/core/QcBlockPos.h"
#include "common/gameplay/QcEnchantingTable.h"
#include "common/protocol/QcMessages.h"

class QcBlockScreens;
class QcServerInventory;
class QcServerSounds;
struct QcServerClient;
class QcServerState;

// The enchanting table's screen in the server (decision E-864), as in the reference game: using a
// table opens it; the player puts an item into the item slot (machineInputSlot) and lapis lazuli
// into the lapis slot (machineFuelSlot) and sees three offers, whose levels depend on the
// bookshelves around the table (QcEnchantingTable::shelvesAround) and whose enchantments follow from
// the player's enchantment seed. Taking offer n (0–2) needs its level, costs n + 1 levels and n + 1
// lapis (nothing in creative mode), enchants the item and draws a new seed. The screen closes when
// the table is gone, out of reach, the player died or became a spectator; the slots then go back.
class QcServerEnchanting
{
public:
    QcServerEnchanting(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory, QcBlockScreens &screens);
    // The closer registered with QcBlockScreens refers to this object.
    QcServerEnchanting(const QcServerEnchanting &) = delete;
    QcServerEnchanting &operator=(const QcServerEnchanting &) = delete;

    // Opens the screen of the table at `pos`; false when it is no enchanting table. Any other screen
    // closes; the same table again keeps its slots.
    bool open(QcServerClient &client, QcBlockPos pos);
    // Takes offer `request.slot` when the player can pay for it; the client gets the slots either way.
    void choose(QcServerClient &client, QcMsg::ChooseEnchantment request);
    // Gives the slots back and forgets the screen; `notify` tells the client (the server closed it).
    void close(QcServerClient &client, bool notify);
    // Once per tick: closes the screens that may not stay open.
    void update();

    // The offers the player sees at the open table now; none without an open table. Draws the
    // player's seed on first use.
    [[nodiscard]] static QcEnchantingTable::Offers offersFor(QcServerState &state, QcServerClient &client);
    // The state message for the open table (slots, shelves, clues); QcServerInventory::send sends it
    // with every inventory state while the table is open.
    [[nodiscard]] static QcMsg::EnchantingState stateOf(QcServerState &state, QcServerClient &client);

private:
    // Whether the player's table still stands within reach and the player lives and acts.
    bool canStayOpen(const QcServerClient &client) const;
    // Whether the block at `pos` is an enchanting table.
    static bool isTable(const QcServerState &state, QcBlockPos pos);
    // The bookshelves that count for the table at `pos`.
    static int shelvesAt(const QcServerState &state, QcBlockPos pos);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
    QcBlockScreens &m_screens;
};
