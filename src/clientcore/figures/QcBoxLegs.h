// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/mobs/QcMobDefinition.h"
#include "common/core/QcVec3.h"

#include <QHash>
#include <QList>


// The legs of a creature built from static boxes (decision E-888), found in its parts, so every
// walking creature of the content and of mods swings its legs without naming them: a leg stands on
// the lowest level of the model and is at least 1.5 times as tall as it is wide and deep; a model
// needs at least two. Flat parts on the ground touching a leg (a chicken's feet) move with it.
// Legs on one side alternate from front to back, and the first leg of the left side swings with
// the first of the right side in opposite phase, so four legs walk in diagonal pairs.
//
// A leg assembly (decision E-895) is a leg whose top carries a horizontal segment reaching inwards
// along x to the body (a spider's leg: the segment out from the body and the post down at its outer
// end). The whole assembly turns around the vertical axis at the body joint (the inner end of the
// segment) instead of swinging forward: its feet move along the side of the body, like the
// spiders of the reference game, instead of hitting the next leg after a few degrees.
class QcBoxLegs
{
public:
    // The widest turn of a leg assembly around the vertical axis, in degrees: neighbouring legs of
    // a side turn against each other and may overlap a little at their feet, as in the reference.
    static constexpr double maxLegYaw = 18.0;

    // The axis a moving part turns around.
    enum class Axis : quint8 {
        Pitch, // across the body (x): a leg swings forward and back
        Yaw, // upright (y): a leg assembly turns along the side of the body
    };

    // How one part moves with the walk: turned around `axis` through `hip` by the swing angle times
    // `phase` (1 or -1; 0 for parts that are not legs). Around Pitch, phase 1 moves the foot forward
    // (-z); around Yaw, phase 1 turns the assembly so a left foot moves forward and a right foot back.
    struct Swing
    {
        QcVec3 hip;
        double phase = 0.0;
        Axis axis = Axis::Pitch;

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
    // for long legs close together (cows, sheep); at most maxLegYaw for a model with leg
    // assemblies (spiders).
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
