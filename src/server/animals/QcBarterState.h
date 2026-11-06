// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcItemStack.h"

#include <QHash>
#include <QList>

/*! \brief What a creature that barters (QcMobDefinition::barter, decision E-1081) holds and seeks:
    the item it looks at (\c {admiring}, empty for none) for \c {admireSeconds} more, the player
    who gave it (\c {from}, entity id, 0 for none or unknown), the loved items it keeps
    (\c {hoard}, dropped when it dies), the item lying around it walks to (\c {sought}, entity id,
    0 for none) since the game tick \c {soughtSince}, and the items it gave up on, each with the
    game tick it may try again (\c {ignored}). Only the first, second and fourth field are saved
    with the creature (QcEntityCodec::Component::Barter, saved()). \qcfields */
struct QcBarterState
{
    QcItemStack admiring;
    double admireSeconds = 0.0;
    quint32 from = 0;
    QList<QcItemStack> hoard;
    quint32 sought = 0;
    quint64 soughtSince = 0;
    QHash<quint32, quint64> ignored;

    /*! Returns whether it looks at an item now. */
    bool admires() const { return !admiring.isEmpty(); }
    /*! Returns whether it holds anything at all. */
    bool holds() const { return admires() || !hoard.isEmpty(); }
    /*! Returns whether it walks to an item lying around now. */
    bool seeks() const { return sought != 0; }
    /*! Returns the part of this state that goes with the creature into a save or another
        dimension. */
    QcBarterState saved() const
    {
        QcBarterState kept;
        kept.admiring = admiring;
        kept.admireSeconds = admireSeconds;
        kept.hoard = hoard;
        return kept;
    }

    bool operator==(const QcBarterState &) const = default;
};
