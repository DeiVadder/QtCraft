// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"
#include "server/dimensions/QcPortalLinks.h"

#include <optional>

class QcServerBlocks;
class QcServerDimension;
class QcServerExplosions;
class QcServerInventory;
class QcServerSounds;
class QcServerState;
struct QcServerClient;

/*! \brief Soul anchors and beds where they do not belong (decisions E-1112, E-1113).

    Using a soul anchor (QcAnchorDefinition) with its fuel charges it by one, up to four. Used
    otherwise while charged, it sets the player's respawn point in a dimension where anchors work
    (QcDimensionDefinition::anchorsWork) and explodes anywhere else: the block goes and a blast of
    explosionPower with fire follows, as in the reference. A bed used in a dimension where beds
    explode (QcDimensionDefinition::bedsExplode) does the same. A respawn at an anchor takes one
    charge; an anchor gone, empty or without room beside it sends the player to the world spawn
    and forgets it. */
class QcServerAnchors
{
public:
    /*! The power of the blast of an anchor or a bed (the reference's 5; TNT has 4). */
    static constexpr double explosionPower = 5.0;

    /*! Where a respawn at an anchor puts the player: a dimension and a point in it. */
    using Respawn = QcPortalLinks::Destination;

    QcServerAnchors(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks, QcServerInventory &inventory,
                    QcServerExplosions &explosions);

    /*! Handles a use of \a client on the block at \a pos (not sneaking, within reach). Returns
        \c false when it is no anchor or bed that reacts, so the item acts as usual. */
    bool use(QcServerClient &client, QcBlockPos pos);
    /*! Returns where the dead \a client respawns at his anchor and takes its charge; none when he
        has none, or it is gone, empty or blocked (then he is told and the anchor forgotten). */
    std::optional<Respawn> respawnAt(QcServerClient &client);

private:
    // A use of the anchor `block` at `pos`.
    bool useAnchor(QcServerClient &client, QcBlockPos pos, const QcBlockDefinition &anchor);
    // One charge more for the anchor `anchor` at `pos`, the fuel taken from the hand.
    void charge(QcServerClient &client, QcBlockPos pos, const QcBlockDefinition &anchor);
    // `client` respawns at the anchor at `pos` of the current dimension from now on.
    void bind(QcServerClient &client, QcBlockPos pos, const QcBlockDefinition &anchor);
    // The bed `block` at `pos` explodes where beds do; false elsewhere.
    bool useBed(QcBlockPos pos, QcBlockId block);
    // Removes the block at `pos` (and `other`, the other half of a bed) and sets off the blast.
    void blowUp(QcBlockPos pos, std::optional<QcBlockPos> other);
    // The anchor at `pos` of the current dimension, if it holds a charge.
    std::optional<QcBlockId> chargedAnchor(QcBlockPos pos) const;
    // Feet of a respawn beside the anchor at `pos`: around it at its height, below and above it,
    // then on top of it.
    std::optional<QcVec3> besideAnchor(QcBlockPos pos) const;
    // Tells `client` that his anchor is of no use and forgets it.
    void forget(QcServerClient &client);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerBlocks &m_blocks;
    QcServerInventory &m_inventory;
    QcServerExplosions &m_explosions;
};
