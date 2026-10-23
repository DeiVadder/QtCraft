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

// The anvil's screen in the server (decision E-866), as in the reference game: using an anvil opens
// it; the player puts a stack into the left slot (machineInputSlot) and optionally material, a
// second item of the kind or nothing into the right slot (machineFuelSlot), types a name and takes
// the output (QcAnvil::result) for its levels (nothing in creative mode). Taking it uses up the left
// stack and what the right one gave, and wears the anvil one step with QcAnvil::wearChance
// (QcBlockDefinition::wearsInto; the last step breaks it). The screen closes when the anvil is gone,
// out of reach, the player died or became a spectator; the slots then go back.
class QcServerAnvil
{
public:
    QcServerAnvil(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory, QcBlockScreens &screens, QcServerBlocks &blocks);
    // The closer registered with QcBlockScreens refers to this object.
    QcServerAnvil(const QcServerAnvil &) = delete;
    QcServerAnvil &operator=(const QcServerAnvil &) = delete;

    // Opens the screen of the anvil at `pos`; false when it is no anvil. Any other screen closes;
    // the same anvil again keeps its slots.
    bool open(QcServerClient &client, QcBlockPos pos);
    // Sets the text of the name field; the client gets the new AnvilState (not the inventory).
    void rename(QcServerClient &client, const QcMsg::RenameAnvil &request);
    // Takes the output when it is still the output and cost the client saw and the player can pay.
    void forge(QcServerClient &client, const QcMsg::ForgeAnvil &request);
    // Gives the slots back and forgets the screen; `notify` tells the client (the server closed it).
    void close(QcServerClient &client, bool notify);
    // Once per tick: closes the screens that may not stay open.
    void update();

    // What the open anvil makes now; empty without an open anvil.
    [[nodiscard]] static QcAnvil::Result resultFor(const QcServerState &state, const QcServerClient &client);
    // The state message for the open anvil; QcServerInventory::send sends it with every inventory
    // state while the anvil is open.
    [[nodiscard]] static QcMsg::AnvilState stateOf(const QcServerState &state, const QcServerClient &client);
    // Wears the anvil at `pos` one step: into QcBlockDefinition::wearsInto, or it breaks.
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
