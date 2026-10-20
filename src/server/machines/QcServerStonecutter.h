#pragma once

#include "common/core/QcBlockPos.h"
#include "common/protocol/QcMessages.h"

class QcBlockScreens;
class QcServerInventory;
class QcServerSounds;
struct QcServerClient;
class QcServerState;

// The stonecutter's screen in the server (decision E-785), with slots as in the reference: using a
// stonecutter opens it; the player puts stone into the input slot (QcServerInventory, slot
// machineInputSlot), chooses one of its stonecutting recipes (decision E-784) and takes the result
// from the output slot (machineOutputSlot), which uses up one input item; a shift-click takes as
// often as the input and the inventory's room allow. The screen closes when the stonecutter is gone,
// out of reach, the player died or became a spectator; the input then goes back to the player.
class QcServerStonecutter
{
public:
    QcServerStonecutter(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory, QcBlockScreens &screens);
    // The closer registered with QcBlockScreens refers to this object.
    QcServerStonecutter(const QcServerStonecutter &) = delete;
    QcServerStonecutter &operator=(const QcServerStonecutter &) = delete;

    // Opens the screen of the stonecutter at `pos` for the player; false when it is no stonecutter.
    // The input of a screen open elsewhere goes back first; an open machine, chest, cart or trade closes.
    bool open(QcServerClient &client, QcBlockPos pos);
    void select(QcServerClient &client, const QcMsg::SelectStonecutterRecipe &request);
    // A move out of the output slot of the open stonecutter: takes the output into the inventory
    // slot `move.to` (whole, or nothing). False when the move is no such move (another slot, or no
    // stonecutter open), which is then the inventory's.
    bool takeOutput(QcServerClient &client, QcMsg::MoveInventoryItem move);
    // A shift-click on the output slot: takes the output into the inventory as long as the input
    // lasts and each whole output finds room. False when `slot` is not that slot.
    bool takeAllOutput(QcServerClient &client, int slot);
    // Gives the input back and forgets the screen; `notify` tells the client (the server closed it).
    // A client that left gets nothing sent.
    void close(QcServerClient &client, bool notify);
    // Once per tick: closes the screens that may not stay open.
    void update();

private:
    // Whether the player's stonecutter still stands within reach and the player lives and acts.
    bool canStayOpen(const QcServerClient &client) const;
    // Whether `slot` is the open stonecutter's output slot (at most one screen is open, so no
    // machine or chest is).
    static bool isOutputSlot(const QcServerClient &client, int slot);
    // Takes the output once into the inventory as a whole, on a copy; false when there is no output
    // or no room for all of it.
    bool takeOnce(QcServerClient &client) const;
    // The output was taken `times` times: the stonecutter sounds, the player gets the slots.
    void tookOutput(QcServerClient &client, int times);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
    QcBlockScreens &m_screens;
};
