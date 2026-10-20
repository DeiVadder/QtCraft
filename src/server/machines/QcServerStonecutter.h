#pragma once

#include "common/content/definitions/QcSmeltingRecipe.h"
#include "common/core/QcBlockPos.h"
#include "common/protocol/QcMessages.h"

class QcServerInventory;
class QcServerSounds;
struct QcServerClient;
class QcServerState;

// The stonecutter's screen in the server (decision E-785): using a stonecutter opens it, and the
// player cuts stone blocks from the inventory by the content's stonecutting recipes (decision E-784)
// straight into the inventory, once or as often as the input and the room allow. The screen closes
// when the stonecutter is gone, out of reach or the player died.
class QcServerStonecutter
{
public:
    QcServerStonecutter(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory);

    // Opens the screen of the stonecutter at `pos` for the player; false when it is no stonecutter.
    bool open(QcServerClient &client, QcBlockPos pos);
    void cut(QcServerClient &client, const QcMsg::CutStone &request);
    // Forgets the open stonecutter; `notify` tells the client (the server closed it).
    void close(QcServerClient &client, bool notify);
    // Once per tick: closes the screens that may not stay open.
    void update();

private:
    // Whether the player's stonecutter still stands within reach and the player lives.
    bool canStayOpen(const QcServerClient &client) const;
    // Cuts once; false when the input is missing or the result has no room.
    bool cutOnce(QcServerClient &client, const QcSmeltingRecipe &recipe) const;

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
};
