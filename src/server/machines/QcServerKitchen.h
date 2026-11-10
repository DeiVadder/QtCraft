// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/protocol/QcMessages.h"

class QcPlayerHealth;
class QcServerBlocks;
class QcServerInventory;
class QcServerSounds;
class QcServerWorld;
struct QcServerClient;
class QcServerState;

/*! \brief Cake, campfires and cauldrons in the server (decisions E-772, E-773, E-1065).

    Using cake eats one bite of it when the player is hungry; the last bite removes it. Using a
    lit campfire with an item it can cook puts one item on it (QcCampfires); a shovel or a bucket
    of a liquid that does not burn puts it out (its block becomes \c {extinguishesInto}, the
    items on it drop), and an unlit one is lit again by flint and steel, a dispenser or fire
    (light()). In the tick the items cook for the campfire's time and drop beside it when done,
    and clients that have the chunk hear what lies on each campfire (QcMsg::CampfireDisplay).
    Using a cauldron with a bottle or bucket takes water out of it or puts water in
    (QcCauldronDefinition). */
class QcServerKitchen
{
public:
    QcServerKitchen(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks, QcServerInventory &inventory,
                    QcPlayerHealth &health);

    /*! Handles a use of the held item of \a client on the block at \a pos (not sneaking, within
        reach). Returns \c false when it is none of the uses above, so the item acts as usual. */
    bool use(QcServerClient &client, QcBlockPos pos);
    /*! Lights the unlit campfire at \a pos. Returns \c false when there is none. */
    bool light(QcBlockPos pos);
    /*! Cooks the items on the campfires and tells the clients what changed. */
    void tick();

    /*! Returns what lies on the campfire at \a pos of \a world. */
    [[nodiscard]] static QcMsg::CampfireDisplay displayOf(const QcServerWorld &world, QcBlockPos pos);

private:
    bool eatBite(QcServerClient &client, QcBlockPos pos, QcBlockId block);
    // What the held item does to the cauldron at `pos`; false when nothing, so it acts as usual.
    bool useCauldron(QcServerClient &client, QcBlockPos pos, QcBlockId block);
    // Sets the cauldron at `pos` to `level` with a sound, and turns the held item into `item`.
    void changeCauldron(QcServerClient &client, QcBlockPos pos, const QString &level, const QString &item);
    bool putOnCampfire(QcServerClient &client, QcBlockPos pos, QcBlockId block);
    bool putOut(QcServerClient &client, QcBlockPos pos, QcBlockId block);
    // Whether the held item puts out a campfire: a shovel, or a bucket of a liquid that does not burn.
    bool putsOut(const QcItemDefinition &held) const;
    // One step of the campfire at `pos`; its cooked items drop.
    void cook(QcBlockPos pos);
    void announceChanges();

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerBlocks &m_blocks;
    QcServerInventory &m_inventory;
    QcPlayerHealth &m_health;
};
