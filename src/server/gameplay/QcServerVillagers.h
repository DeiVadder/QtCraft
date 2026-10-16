#pragma once

#include "QcBlockPos.h"
#include "QcVillagerBehaviour.h"

class QcServerBlocks;
class QcServerSounds;
class QcServerState;
struct QcServerClient;

// What villagers do to the world (decision E-411), carried out after the creatures' tick: the
// wooden doors they open and close (QcOpeners: both halves, the door's sound, never onto a body; a
// door that cannot close yet is tried again), the ripe crops farmers harvest and plant again in one
// step (the crop goes back to its first stage; the harvest counts as the farmer's produce, no items
// drop), and clients learning a villager's new look. A player using a villager makes it stand and
// face them: the hook for trading (stage 37.2c).
class QcServerVillagers
{
public:
    QcServerVillagers(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks);

    void apply();
    // The player used the creature `id`; false when it is no villager within reach.
    bool talk(const QcServerClient &client, quint32 id);

private:
    // Flips the door at `pos` to `open` when it is a wooden door that is not so yet; false when it
    // stays as it is although it should change.
    bool setDoor(QcBlockPos pos, bool open);
    // Opens or closes the door of `action`; when it cannot, the villager tries again.
    void door(const QcVillagerBehaviour::Action &action);
    void harvest(quint32 villager, QcBlockPos pos);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerBlocks &m_blocks;
};
