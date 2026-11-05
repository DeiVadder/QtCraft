// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/protocol/QcMessages.h"
#include "common/world/QcEntityKind.h"
#include "server/dimensions/QcPortalIndex.h"
#include "server/dimensions/QcPortalLinks.h"
#include "server/dimensions/QcTravellers.h"

#include <QList>

class QcServerInventory;
class QcServerSounds;
struct QcServerClient;

/*! \brief Portals in the server tick (decisions E-1020 to E-1025): lighting them, carrying players,
    creatures, items and minecarts through them, and their hum.

    Fire lit in an empty frame (QcServerFire::ignite asks lightByFire() first, so flint and steel,
    lava, lightning and later fire charges all light a portal) or the last insert put into a ring
    (use()) fills the frame with the portal's block. Once per tick and dimension, tick() looks who
    stands in a portal: a player after the wait time, the others at once (QcTravellers); the
    journeys run after every dimension ticked (travel()), so no dimension opens while the
    dimensions tick. A portal whose frame breaks goes out by the support rule of its block
    (QcPortalShapes::isHeld). */
class QcServerPortals
{
public:
    /*! Ticks between two looks for portals whose block went away. */
    static constexpr int forgetEveryTicks = 100;
    /*! Ticks between two hums of a portal near a player, and the blocks within which a player
        hears it. \qcshared */
    static constexpr int humEveryTicks = 80;
    static constexpr double humRange = 16.0;

    QcServerPortals(QcServerState &state, QcServerBlocks &blocks, QcServerSounds &sounds, QcServerInventory &inventory);

    /*! Fire is lit at \a pos in the current dimension: when \a pos is in an empty complete frame
        of a portal lit by fire that leads from or to here, lights that portal instead. Returns
        whether it did. */
    bool lightByFire(QcBlockPos pos);
    /*! Handles a use of the held item of \a client (\a message) on a frame block of a portal lit
        by that item: puts the insert in and lights a complete ring. Returns false when the use is
        something else. */
    bool use(QcServerClient &client, const QcMsg::UseItem &message);
    /*! Looks who stands in a portal of the current dimension and notes who travels; the hum of the
        portals near players; forgets portals that went out. */
    void tick();
    /*! Carries out the journeys tick() noted, in every dimension. */
    void travel();
    /*! Returns who waits and who rests. */
    const QcTravellers &travellers() const { return m_travellers; }
    /*! Returns where portals lead, for tests and commands. */
    QcPortalLinks &links() { return m_links; }

private:
    // A journey tick() noted.
    struct Departure
    {
        QcEntityKind kind = QcEntityKind::Player;
        quint32 id = 0;
        QcServerDimension *from = nullptr;
        const QcPortalDefinition *portal = nullptr;
        QcBlockPos cell;
    };

    // The portal whose block stands at `cell` of the current dimension; null for none.
    const QcPortalDefinition *portalAt(QcBlockPos cell) const;
    void notePlayers();
    void noteEntities();
    // Notes an entity of `kind` at `position` when it is in a lit cell of `entry` and rested.
    void noteEntity(QcEntityKind kind, quint32 id, const QcVec3 &position, const QcPortalIndex::Entry &entry);
    void hum();
    void forgetGone();
    void carry(const Departure &departure);
    // The player of `departure`, still playing in the dimension he left from; null otherwise.
    QcServerClient *playerOf(const Departure &departure);

    QcServerState &m_state;
    QcServerBlocks &m_blocks;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
    QcPortalLinks m_links;
    QcTravellers m_travellers;
    QList<Departure> m_due;
};
