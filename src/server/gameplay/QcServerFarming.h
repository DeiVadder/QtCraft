#pragma once

#include "QcAabb.h"
#include "QcBlockFace.h"
#include "QcBlockPos.h"
#include "QcItemDefinition.h"
#include "QcMobBreeding.h"

class QcServerBlocks;
class QcServerInventory;
class QcServerSounds;
struct QcServerClient;
struct QcServerState;

// What players do for farming and what animals do about it: tilling soil with a hoe (decision
// E-290), feeding animals and the births that follow (decision E-293).
class QcServerFarming
{
public:
    QcServerFarming(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks, QcServerInventory &inventory);

    // Tills the clicked block when the held item `tills` and the block has `tillsInto`, on any face
    // but the bottom, with air above, within reach. False when the click is no tilling (then other
    // uses of the item may follow).
    bool till(QcServerClient &client, QcBlockPos target, QcBlockFace face, const QcItemDefinition *held);
    enum class Feeding {
        // The creature does not eat the held item at all.
        NotItsFood,
        // It eats it, but not now (in love, resting) or is out of reach or hidden: nothing happens.
        Refused,
        Fed,
    };

    // Feeds the creature with the held item when it eats that (in love or growing up).
    Feeding feed(QcServerClient &client, quint32 mobId);
    // The box players see and aim at: young animals at QcMsg::youngScale (collision stays adult).
    static QcAabb shownBox(const QcMobDefinition &definition, const QcMobManager::Mob &mob);
    // Breeding timers and births of one simulated tick.
    void tick();

private:
    // One use of the held tool, except in creative mode.
    void wearHeld(QcServerClient &client);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerBlocks &m_blocks;
    QcServerInventory &m_inventory;
    QcMobBreeding m_breeding;
};
