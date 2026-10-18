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
    // The box players see and aim at: young animals at QcMsg::youngScale (collision stays adult).
    static QcAabb shownBox(const QcMobDefinition &definition, const QcCreature &mob);
    // Breeding timers, births and laid eggs of one simulated tick.
    void tick();
    // A young one of `type` at `position` that is kept with the world (an egg's chick, decision
    // E-450), within the same limits as births (QcServerLimits breedingCrowd, bredPerKind); its id,
    // 0 when there is no room.
    static quint32 spawnYoung(QcServerState &state, int type, const QcVec3 &position);

private:
    // What creatures lay in this tick (QcMobLaying, decision E-452), dropped as items.
    void layEggs();

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
    QcMobBreeding m_breeding;
    QcPetCare m_pets;
};
