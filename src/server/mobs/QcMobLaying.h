#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "server/entities/QcCreature.h"

#include <QList>
#include <QRandomGenerator>

// Creatures laying now and then (a chicken's eggs, decision E-452), on QcCreature::layTimer: an
// awake adult of a kind with QcMobDefinition::laying counts its time down and lays when it runs out,
// then chooses the next time evenly between the kind's minSeconds and maxSeconds. Young ones and
// dormant creatures (their chunk not simulated) do not count.
class QcMobLaying
{
public:
    // An item laid at a creature's feet.
    struct Laid
    {
        QString item;
        QString sound;
        QcVec3 position;
    };

    // Advances the timers of `mobs` by `dt` seconds; returns what was laid.
    static QList<Laid> tick(const QcContentRegistry &content, QList<QcCreature> &mobs, QRandomGenerator &random, double dt);
    // A time until the next laying of a kind, evenly between its minSeconds and maxSeconds.
    static double nextTime(const QcLayingDefinition &laying, QRandomGenerator &random);
};
