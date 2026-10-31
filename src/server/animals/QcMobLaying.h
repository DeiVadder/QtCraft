// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "server/entities/QcCreature.h"

#include <QList>
#include <QRandomGenerator>

/*! \brief Creatures laying now and then (a chicken's eggs, decision E-452), on
    QcCreature::layTimer: an awake adult of a kind with QcMobDefinition::laying counts its time down
    and lays when it runs out, then chooses the next time evenly between the kind's minSeconds and
    maxSeconds.

    Young ones and dormant creatures (their chunk not simulated) do not count. */
class QcMobLaying
{
public:
    /*! \brief An item laid at a creature's feet. */
    struct Laid
    {
        /*! The content name of the item. */
        QString item;
        /*! The sound it makes (content name). */
        QString sound;
        /*! Where it lies. */
        QcVec3 position;
    };

    /*! Advances the timers of \a mobs by \a dt seconds (kinds from \a content, new times drawn
        from \a random); returns what was laid. */
    static QList<Laid> tick(const QcContentRegistry &content, QList<QcCreature> &mobs, QRandomGenerator &random, double dt);
    /*! Returns a time until the next laying of \a laying, drawn from \a random evenly between its
        minSeconds and maxSeconds. */
    static double nextTime(const QcLayingDefinition &laying, QRandomGenerator &random);
};
