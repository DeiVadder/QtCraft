// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/mobbrains/QcMobBrain.h"

class QRandomGenerator;

/*! \brief The brain of drifters (QcMobDefinition::Ai::Drifter, decision E-1053): creatures that
    hover (QcMobHover) and find no paths. They hold a heading and turn at walls; a hostile one
    charges at a player it sees within the range of its \c {archer} settings and shoots, in a
    volley when they say so; a ridden one (decision E-1054) goes where its rider looks while he
    holds the kind's steering item. One that rises to its target (QcHoverDefinition::risesToTarget,
    decision E-1056) stays where it is while it has one, keeps its eyes about level with the
    target's and strikes it from close by. */
class QcDrifterBrain final : public QcMobBrain
{
public:
    /*! How far ahead (metres) a drifter that keeps to lava looks for it. */
    static constexpr double lavaLookAhead = 3.0;
    /*! How many headings such a drifter tries before it takes the last. */
    static constexpr int headingTries = 5;

    /*! Seconds a drifter that rises to its target keeps one height below the target's eyes. */
    static constexpr double heightOffsetSeconds = 5.0;
    /*! The mean and the deviation of that height (metres, the original game's blaze). \qcshared */
    static constexpr double heightOffsetMean = 0.5;
    static constexpr double heightOffsetDeviation = 3.0;

    /*! Draws its headings and the spread of its shots from \a random. */
    explicit QcDrifterBrain(QRandomGenerator &random);

    QcPhysics::Steering think(QcCreature &mob, const QcMobDefinition &definition, const Context &context, int thinkTicks) override;

private:
    QcPhysics::Steering drift(QcCreature &mob, const QcMobDefinition &definition, const Context &context, double seconds);
    void charge(QcCreature &mob, const QcMobDefinition &definition, const QcMobManager::PlayerView &target, const Context &context,
                double seconds);
    QcVec3 newHeading(const QcCreature &mob, const QcMobDefinition &definition, const Context &context);
    // A drifter that rises to its target (decision E-1056) with `target` this tick.
    QcPhysics::Steering holdAt(QcCreature &mob, const QcMobDefinition &definition, const QcMobManager::PlayerView &target, const Context &context,
                               double seconds);
    // The velocity of a shot along `aim`, with the kind's scatter or spread.
    QcVec3 shotAt(const QcArcherDefinition &archer, const QcVec3 &aim);
    // A number of the normal distribution with mean 0 and deviation 1.
    double normalDraw();

    QRandomGenerator &m_random;
};
