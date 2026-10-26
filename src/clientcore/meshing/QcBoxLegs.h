#pragma once

#include "common/content/definitions/QcMobDefinition.h"
#include "common/core/QcVec3.h"

#include <QHash>
#include <QList>


// The legs of a creature built from static boxes (decision E-888), found in its parts, so every
// walking creature of the content and of mods swings its legs without naming them: a leg stands on
// the lowest level of the model and is at least 1.5 times as tall as it is wide and deep; a model
// needs at least two. Flat parts on the ground touching a leg (a chicken's feet) move with it.
// Legs on one side alternate from front to back, and the first leg of the left side swings with
// the first of the right side in opposite phase, so four legs walk in diagonal pairs.
class QcBoxLegs
{
public:
    // How one part moves with the walk: turned around the x axis through `hip` by the swing angle
    // times `phase` (1 or -1; 0 for parts that are not legs).
    struct Swing
    {
        QcVec3 hip;
        double phase = 0.0;

        // `local` (a point of the part, model space) with the part turned by `degrees` times the phase.
        [[nodiscard]] QcVec3 apply(const QcVec3 &local, double degrees) const;
    };

    [[nodiscard]] static QcBoxLegs of(const QList<QcMobPart> &parts);

    // One entry per part, in the order of the parts; empty when the model has no legs.
    const QList<Swing> &swings() const { return m_swings; }
    // The mean height of the legs in model metres; 0 without legs.
    double length() const { return m_length; }
    // The widest swing in degrees at which neighbouring legs of one side, swinging against each
    // other, keep 10 % of the gap between them: QcAnimator::maxLegSwing for most creatures, less
    // for long legs close together (cows, sheep, spiders).
    double maxSwing() const { return m_maxSwing; }

private:
    QList<Swing> m_swings;
    double m_length = 0.0;
    double m_maxSwing = 0.0;
};

// The legs of the box models drawn, found once per model instead of every frame. A model is known
// by its list of parts: the cache keeps a copy of the list, so its shared data stays alive and the
// address stays unique while the entry exists; a list that changes is a new list.
class QcBoxLegsCache
{
public:
    [[nodiscard]] const QcBoxLegs &of(const QList<QcMobPart> &parts);

private:
    struct Entry
    {
        QList<QcMobPart> parts;
        QcBoxLegs legs;
    };

    QHash<const QcMobPart *, Entry> m_entries;
};
