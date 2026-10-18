#pragma once

#include "common/core/QcBlockPos.h"
#include "server/villages/QcVillagerBehaviour.h"

#include <QHash>
#include <QSet>

class QcServerBlocks;
class QcServerSounds;
class QcServerState;

// What villagers do to the world (decision E-411), carried out after the creatures' tick: the
// wooden doors they open and close (QcOpeners: both halves, the door's sound, never onto a body; a
// door that cannot close yet is tried again), the ripe crops farmers harvest and plant again in one
// step (the crop goes back to its first stage; the harvest counts as the farmer's produce, no items
// drop), the food adult villagers pick up from the ground every foodTicks ticks
// (QcVillagerBreedingSettings::foods, decision E-490: it counts as produce, so throwing bread feeds
// them), and clients learning a villager's new look. Without the game rule mobGriefing villagers
// neither harvest nor pick anything up, like in the reference game, and confined villagers or ones
// without a mind never do (QcCreature::mayGrief, decision E-510). Trading is QcServerTrading's.
class QcServerVillagers
{
public:
    // Villagers pick up food lying this close to their feet (horizontally; up to one block above
    // or below).
    static constexpr double foodReach = 1.0;
    // They look for food lying at their feet every this many ticks.
    static constexpr int foodTicks = 4;

    QcServerVillagers(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks);

    void apply();

private:
    // Flips the door at `pos` to `open` when it is a wooden door that is not so yet; false when it
    // stays as it is although it should change.
    bool setDoor(QcBlockPos pos, bool open);
    // Opens or closes the door of `action`; when it cannot, the villager tries again.
    void door(const QcVillagerBehaviour::Action &action);
    void harvest(quint32 villager, QcBlockPos pos);
    // Adult villagers take the food lying at their feet while they hold less than they want.
    void pickUpFood();

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerBlocks &m_blocks;
    // Items some villager species eats, and what each item is worth to the villagers of a creature
    // kind (QcVillagerBreedingSettings::foods).
    QSet<QcItemId> m_foods;
    QHash<int, QHash<QcItemId, int>> m_worth;
    int m_foodTick = 0;
};
