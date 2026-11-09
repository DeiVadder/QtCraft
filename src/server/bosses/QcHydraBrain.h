// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/mobbrains/QcMobBrain.h"

#include <optional>

class QRandomGenerator;
struct QcHydraDefinition;
struct QcHydraState;

/*! \brief The brain of the charred hydra (QcMobDefinition::Ai::Hydra, decision E-1126; the role
    model's wither): a hovering boss with three heads.

    \list
    \li While it charges (QcHydraState::charging) it stays where it is and heals; at the end it
        asks the server for its blast (QcHydraState::blastDue).
    \li Its main head chooses the player or living creature that is not undead nearest to it
        (one that struck it first) within targetRange metres sideways and targetHeight above or
        below its body, keeps it while it stays within targetRange, and fires a skull at it every
        QcHydraDefinition::attackSeconds while it sees it within attackRange; by
        QcHydraDefinition::chargedChance the skull is a charged one.
    \li Each side head looks every sideMinTicks to sideMinTicks + sideSpreadTicks ticks: it fires
        at its own target while it sees it within sideRange (then waits sideShotTicks to
        sideShotTicks + sideShotSpreadTicks), else it picks a random one within sideSearch
        metres sideways and sideSearchHeight up and down; after more than sideIdleShots looks
        without a shot it fires a charged skull at a random place near it.
    \li With a target it flies towards it until keepDistance metres away sideways and keeps its
        feet QcHydraDefinition::hoverAbove over the target's, in its armour level with them;
        without one it wanders. It regenerates and, a moment after a fresh hurt, asks the server
        to break the blocks around it (QcHydraState::breakDue, rule mobGriefing).
    \endlist */
class QcHydraBrain final : public QcMobBrain
{
public:
    /*! How far sideways (metres) the main head finds a target and keeps it (the role model's
        follow range). */
    static constexpr double targetRange = 40.0;
    /*! How far above and below its body the main head finds a new target. */
    static constexpr double targetHeight = 4.0;
    /*! How far the side heads keep their targets. */
    static constexpr double sideRange = 30.0;
    /*! How far sideways and up and down the side heads find targets. \qcshared */
    static constexpr double sideSearch = 20.0;
    static constexpr double sideSearchHeight = 8.0;
    /*! Ticks between two looks of a side head: at least sideMinTicks, up to sideSpreadTicks more.
        \qcshared */
    static constexpr int sideMinTicks = 10;
    static constexpr int sideSpreadTicks = 10;
    /*! Ticks a side head waits after a shot at its target: at least sideShotTicks, up to
        sideShotSpreadTicks more. \qcshared */
    static constexpr int sideShotTicks = 40;
    static constexpr int sideShotSpreadTicks = 20;
    /*! Looks without a shot after which a side head fires at a random place. */
    static constexpr int sideIdleShots = 15;
    /*! How far sideways and up and down from its feet such a random place lies. \qcshared */
    static constexpr double idleReach = 10.0;
    static constexpr double idleReachHeight = 5.0;
    /*! How near sideways it comes to its target. */
    static constexpr double keepDistance = 3.0;
    /*! Ticks between two looks of the main head for a target. */
    static constexpr int lookTicks = 10;
    /*! Where the heads sit: the main head this high above the feet, the side heads lower and this
        far to each side. \qcshared */
    static constexpr double mainHeadHeight = 3.0;
    static constexpr double sideHeadHeight = 2.2;
    static constexpr double sideHeadSpacing = 1.3;
    /*! Ticks between two regenerations (the role model's 20). */
    static constexpr int regenerationTicks = 20;
    /*! Ticks a wandering hydra keeps a heading. */
    static constexpr int headingTicks = 80;

    /*! \brief A body a head can aim at: a player or a creature, its feet and height. \qcfields */
    struct Foe
    {
        quint32 id = 0;
        QcVec3 feet;
        double height = 0.0;
    };

    /*! Draws its random choices from \a random. */
    explicit QcHydraBrain(QRandomGenerator &random);

    QcPhysics::Steering think(QcCreature &mob, const QcMobDefinition &definition, const Context &context, int thinkTicks) override;
    /*! Returns true: a boss thinks in any crowd, its charge and its fight never pause. */
    bool thinksInIdleCrowd() const override { return true; }

    /*! Returns where the head \a head (0 the main, 1 and 2 the sides) of \a mob is. */
    [[nodiscard]] static QcVec3 headOf(const QcCreature &mob, int head);
    /*! Returns \a id as a foe of \a hydra in \a context: a player alive in the world who may be
        targeted, or a creature that is awake and not undead; none for anything else. */
    [[nodiscard]] static std::optional<Foe> foeOf(const QcCreature &hydra, quint32 id, const Context &context);

private:
    // The charge: heals, counts down and asks for the blast at its end.
    void charge(QcCreature &mob, const QcMobDefinition &definition, int ticks);
    // Regeneration and the countdown to breaking blocks after a hurt.
    void recover(QcCreature &mob, const QcMobDefinition &definition, const Context &context, int ticks);
    // The main head's target: kept while it is near enough, else the nearest in reach.
    std::optional<Foe> mainTarget(QcCreature &mob, const Context &context, int ticks);
    void sideHead(QcCreature &mob, const Context &context, int side);
    void mainHead(QcCreature &mob, const QcHydraDefinition &hydra, const Context &context, const Foe &target, int ticks);
    // A skull from the head `head` of `mob` towards `point`; charged or not.
    void fire(const QcCreature &mob, const Context &context, int head, const QcVec3 &point, bool charged);
    QcPhysics::Steering chase(QcCreature &mob, const QcMobDefinition &definition, const Foe &target);
    QcPhysics::Steering wander(QcCreature &mob, const QcMobDefinition &definition, int ticks);
    // The foes within `reach` metres sideways and `height` up and down of the body of `mob`.
    QList<Foe> foesNear(const QcCreature &mob, const Context &context, double reach, double height) const;

    QRandomGenerator &m_random;
};
