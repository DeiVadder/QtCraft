// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Protocol messages of the beacon (decisions E-1142, E-1143). Part of the QcMessage variant
// (QcMessages.h).

#include "common/core/QcBlockPos.h"
#include "common/gameplay/QcBeacon.h"
#include "common/gameplay/QcItemStack.h"

#include <QList>
#include <QString>

namespace QcMsg {

/*! \brief Server -> client: the player opened the beacon at \c {pos} (decision E-1142);
    BeaconState follows.

    Its payment slot is machineInputSlot of MoveInventoryItem and QuickMoveItem while no machine
    or chest is open. */
struct OpenBeacon
{
    /*! The beacon's block. */
    QcBlockPos pos;
};

/*! \brief Server -> client: the open beacon's payment slot, its level (0: no pyramid or no
    beam) and the effects it gives (effect kinds, empty for none). \qcfields */
struct BeaconState
{
    QcItemStack payment;
    int level = 0;
    QString primary;
    QString secondary;
};

/*! \brief Client -> server: pay with the item in the payment slot for \c {primary} and
    \c {secondary} (empty: none); refused unless the beacon's level allows both
    (QcBeacon::mayChoose) and the slot holds a payment item. \qcfields */
struct ChooseBeaconEffects
{
    QString primary;
    QString secondary;
};

/*! \brief Client -> server: the beacon screen closed.

    Server -> client: the beacon closed (broken, out of reach). */
struct CloseBeacon
{
};

/*! \brief Server -> client: the beam of the beacon at \c {pos} (decision E-1143), its sections
    from the beacon up; none while it shines no beam (no pyramid, no sky). Sent for every beacon
    of a chunk with its ChunkData and whenever a beam changes; forgotten with the chunk.
    \qcfields */
struct BeaconBeam
{
    /*! Sections at most in a message (one per block of the world's height). */
    static constexpr qsizetype maxSections = 512;

    QcBlockPos pos;
    QList<QcBeamSection> sections;
};

} // namespace QcMsg
