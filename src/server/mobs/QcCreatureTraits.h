#pragma once

#include "server/mobs/QcMobManager.h"

class QcServerBlocks;
class QcServerSounds;
class QcServerState;

// What creatures with traits do to the world (decision E-460), after the creatures' tick: the blocks
// they took and put down (QcMobManager::takeBlockActions) become ordinary block changes of the server
// (QcServerBlocks: clients, light, neighbour updates, saving), the teleports since the last call
// (QcCreature::teleportedFrom) play their sound at both places and show the clients the new place at
// once, and a creature that dies drops the block it carried. A creature that was not kept with the
// world is kept (QcMobManager::keep, QcCreature::keptForBlock) while it carries a block and let go
// when it puts the block down (or drops it, game rule mobGriefing off); one that was kept before (a
// structure placed it, it was named) stays kept.
class QcCreatureTraits
{
public:
    QcCreatureTraits(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks);
    ~QcCreatureTraits();
    QcCreatureTraits(const QcCreatureTraits &) = delete;
    QcCreatureTraits &operator=(const QcCreatureTraits &) = delete;

    void apply();
    // Plays the sounds of the teleports since the last call and sends the creatures anew, so the
    // clients show them at the new place in the same tick: after the creatures' tick and again after
    // the projectiles and burning, which also make creatures teleport.
    void announceTeleports();

private:
    // Takes the block when it is still there, the creature carries none and may change the world
    // (QcCreature::mayGrief).
    void take(const QcMobManager::BlockAction &action);
    // Puts the carried block into the air of the cell when no body is in it and the block holds
    // there (QcBlockRules::isSupported: a flower on soil, a cactus on sand).
    void place(const QcMobManager::BlockAction &action);
    // A creature that may not change the world (game rule mobGriefing off, confined; QcCreature::
    // mayGrief) drops its block as an item instead.
    void drop(const QcMobManager::BlockAction &action);
    // It carries nothing now; kept only for the block, it is no longer kept.
    void letGo(QcCreature &mob);
    // Whether a body of any kind (player, creature, item, cart, stand, projectile, ...) overlaps the
    // cell.
    bool bodyIn(QcBlockPos pos) const;
    // A creature that died drops the item of the block it carried.
    void dropCarried(const QcCreature &mob);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerBlocks &m_blocks;
    QcMobManager::DepartureToken m_departures{};
};
