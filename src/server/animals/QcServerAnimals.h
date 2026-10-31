// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcAabb.h"
#include "server/animals/QcMobBreeding.h"
#include "server/animals/QcPetCare.h"

class QcServerInventory;
class QcServerSounds;
struct QcServerClient;
class QcServerState;

/*! \brief Players feeding animals and the births that follow (decision E-293): food from the
    hand makes an animal fall in love or a young one grow faster, two animals in love in sight of
    each other get a young one (QcMobBreeding), which is kept with the world. */
class QcServerAnimals
{
public:
    /*! \brief What came of feeding a creature. */
    enum class Feeding {
        /*! The creature does not eat the held item at all. */
        NotItsFood,
        /*! It eats it, but not now (in love, resting) or is out of reach or hidden: nothing
            happens. */
        Refused,
        /*! It ate the item. */
        Fed,
    };

    QcServerAnimals(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory);

    /*! Feeds the creature \a mobId with the item held by the player of \a client when it eats
        that (in love or growing up) and returns what came of it. */
    Feeding feed(QcServerClient &client, quint32 mobId);
    /*! Taming, sitting, healing and collars of the tameable creature \a mobId by the player of
        \a client (QcPetCare, decision E-480); returns false when the use is none of these. */
    bool tend(QcServerClient &client, quint32 mobId) { return m_pets.use(client, mobId); }
    /*! Milks the grown creature \a mobId with the milking item held by the player of \a client
        (a cow and a bucket, decision E-500): one of the items becomes QcMobDefinition::milkItem.
        Returns false when the held item does not milk it. */
    bool milk(QcServerClient &client, quint32 mobId);
    /*! Returns the box players see and aim at of \a mob of the kind \a definition: young animals
        at QcMsg::youngScale (collision stays adult). */
    static QcAabb shownBox(const QcMobDefinition &definition, const QcCreature &mob);
    /*! Breeding timers, births and laid eggs of one simulated tick. */
    void tick();
    /*! Spawns in \a state a young one of \a type at \a position that is kept with the world (an
        egg's chick, decision E-450), within the same limits as births (QcServerLimits
        breedingCrowd, bredPerKind); returns its id, 0 when there is no room. */
    static quint32 spawnYoung(QcServerState &state, int type, const QcVec3 &position);
    /*! Returns whether the creature a fish bucket \a item holds (QcItemDefinition::releasesMob,
        decision E-501) may come out in \a state at \a position, within the same limits as births;
        true for an item that holds none. A bucket whose creature may not come out stays full. */
    static bool mayRelease(QcServerState &state, const QcItemDefinition &item, const QcVec3 &position);
    /*! Lets the creature of \a item out in \a state at \a position, kept with the world and
        counted as bred. */
    static void release(QcServerState &state, const QcItemDefinition &item, const QcVec3 &position);
    /*! Returns where the creature of a bucket poured into the cell \a cell comes out. */
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
