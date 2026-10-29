#pragma once

#include "common/core/QcAabb.h"
#include "server/mobs/QcMobBreeding.h"
#include "server/mobs/QcPetCare.h"

class QcServerInventory;
class QcServerSounds;
struct QcServerClient;
class QcServerState;

// Players feeding animals and the births that follow (decision E-293): food from the hand makes an
// animal fall in love or a young one grow faster, two animals in love in sight of each other get a
// young one (QcMobBreeding), which is kept with the world.
class QcServerAnimals
{
public:
    enum class Feeding {
        // The creature does not eat the held item at all.
        NotItsFood,
        // It eats it, but not now (in love, resting) or is out of reach or hidden: nothing happens.
        Refused,
        Fed,
    };

    QcServerAnimals(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory);

    // Feeds the creature with the held item when it eats that (in love or growing up).
    Feeding feed(QcServerClient &client, quint32 mobId);
    // Taming, sitting, healing and collars of tameable creatures (QcPetCare, decision E-480); false
    // when the use is none of these.
    bool tend(QcServerClient &client, quint32 mobId) { return m_pets.use(client, mobId); }
    // Milks a grown creature with the held milking item (a cow and a bucket, decision E-500): one of
    // the items becomes QcMobDefinition::milkItem. False when the held item does not milk it.
    bool milk(QcServerClient &client, quint32 mobId);
    // The box players see and aim at: young animals at QcMsg::youngScale (collision stays adult).
    static QcAabb shownBox(const QcMobDefinition &definition, const QcCreature &mob);
    // Breeding timers, births and laid eggs of one simulated tick.
    void tick();
    // A young one of `type` at `position` that is kept with the world (an egg's chick, decision
    // E-450), within the same limits as births (QcServerLimits breedingCrowd, bredPerKind); its id,
    // 0 when there is no room.
    static quint32 spawnYoung(QcServerState &state, int type, const QcVec3 &position);
    // Whether the creature a fish bucket `item` holds (QcItemDefinition::releasesMob, decision
    // E-501) may come out at `position`, within the same limits as births; true for an item that
    // holds none. A bucket whose creature may not come out stays full.
    static bool mayRelease(QcServerState &state, const QcItemDefinition &item, const QcVec3 &position);
    // Lets the creature of `item` out at `position`, kept with the world and counted as bred.
    static void release(QcServerState &state, const QcItemDefinition &item, const QcVec3 &position);
    // Where the creature of a bucket poured into the cell `cell` comes out.
    static QcVec3 releasePoint(QcBlockPos cell) { return {cell.x + 0.5, cell.y + 0.2, cell.z + 0.5}; }

private:
    // What creatures lay in this tick (QcMobLaying, decision E-452), dropped as items.
    void layEggs();
    // The adventure guide's breeding (decision E-923): the nearest player within breedingCreditReach
    // of the birth counts as the breeder (the game does not note who fed the parents).
    void creditBreeding(const QcMobBreeding::Birth &birth);
    static constexpr double breedingCreditReach = 16.0;

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
    QcMobBreeding m_breeding;
    QcPetCare m_pets;
};
