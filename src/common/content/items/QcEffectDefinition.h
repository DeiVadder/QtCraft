// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QtGlobal>

#include <algorithm>

/*! \brief A kind of status effect (decision E-450), from effects/<name>.json: what it does to a
    body while it lasts (poison: damage now and then, never below \c {minHealth}) and how the HUD
    shows it (icon, colour, hearts in the colour).

    Players and creatures carry effects of these kinds (QcStatusEffects). What the fields add up to
    for a body stands in QcEffectRules (decision E-992). The content sync sends the looks, what the
    client's prediction steps with (\c {speed}, \c {jump}, \c {maxFallSpeed}), the mining time
    (\c {mining}), \c {nightVision} and \c {visibility}; the rest acts on the server only. */
struct QcEffectDefinition
{
    /*! The highest level an effect may have (/effect, onHit "effect", saved effects). */
    static constexpr int maxLevel = 10;
    /*! The largest \c {maxFallSpeed} in m/s. */
    static constexpr double fastestFallLimit = 60.0;
    /*! The largest \c {food} and \c {saturation} per level. */
    static constexpr int maxFoodPerAct = 20;

    /*! Qualified name of the effect (\c {"name"}, required). */
    QString name;
    /*! Translation key of its display name (\c {"translation"}); defaults to
        \c {effect.<namespace>.<name>}. */
    QString translationKey;
    /*! Item whose picture the HUD shows beside the remaining time; empty for a plain badge. */
    QString icon;
    /*! 0xRRGGBB of the badge and, with \c {tintsHearts}, of the hearts while it lasts. */
    quint32 colour = 0xffffff;
    /*! Whether the hearts take \c {colour} while it lasts (\c {"tintsHearts"}). */
    bool tintsHearts = false;
    /*! Damage dealt every \c {periodTicks} ticks at level 1; each level above halves the period
        (the reference's 25 >> (level - 1) for poison), at least one tick. An \c {instant} effect
        deals it once, doubled per level above I. */
    double damage = 0.0;
    /*! Health given back like \c {damage} is dealt (regeneration; instant health once). */
    double heal = 0.0;
    /*! Acts once in the tick after it was given and ends (\c {"instant"}): whatever duration it
        is given with, it lasts that one tick and the HUD does not show it. */
    bool instant = false;
    /*! On undead creatures (QcMobDefinition::undead) \c {heal} hurts and \c {damage} heals
        (\c {"reversedOnUndead"}): instant health and instant damage. */
    bool reversedOnUndead = false;
    /*! Ticks between two hurts at level 1 (\c {"periodTicks"}, 1 to 72000). */
    int periodTicks = 25;
    /*! Its damage never takes the health below this (poison: 1, it never kills). */
    double minHealth = 0.0;
    /*! Share of the speed it adds per level, in [-1, 1] (slowness: -0.15 like the reference,
        decision E-838): walking, swimming and chasing bodies move at 1 + speed × level of their
        speed, at least 0. Flying players are not slowed. */
    double speed = 0.0;
    /*! Food exhaustion per second at level I (hunger: 0.1 like the reference's 0.005 per tick,
        decision E-838), times the level; players only. Server side only. */
    double exhaustion = 0.0;
    /*! Food points per level it gives each time it acts, even to a full bar (\c {"food"},
        saturation: 1 every tick like the reference; decision E-1001); players only. Server side
        only. */
    int food = 0;
    /*! Saturation per level it gives each time it acts (\c {"saturation"}, saturation: 2 like
        the reference), never more than the food points; players only. Server side only. */
    double saturation = 0.0;
    /*! Points per level added to the damage of a body's blows, negative to take away (strength
        3, weakness -4 like the reference); blows never go below 0. Shots do not change. */
    double attack = 0.0;
    /*! Share of the damage it takes away per level (resistance 0.2: level V stops everything),
        in [0, 1]; the void and commands go through. */
    double resistance = 0.0;
    /*! Fire, lava, burning and hot blocks do a body no harm (\c {"fireProof"}). */
    bool fireProof = false;
    /*! A player under water keeps his breath (\c {"waterBreathing"}). */
    bool waterBreathing = false;
    /*! The client draws the world bright for this player (\c {"nightVision"}). */
    bool nightVision = false;
    /*! Below 1 the body is invisible (\c {"visibility"}, in [0, 1]): it is drawn without its skin,
        and creatures notice it within this share of their sight range, times the share of armour
        worn (at least a tenth), like the reference's 0.7. */
    double visibility = 1.0;
    /*! Share of the jump velocity it adds per level, in [0, 2] (jump boost: 0.24, the reference's
        0.1 on 0.42 blocks per tick). A fall hurts one block later per level. */
    double jump = 0.0;
    /*! The fastest a player falls in m/s while it lasts, 0 for no limit; a limited fall does no
        damage (\c {"maxFallSpeed"}, slow falling). */
    double maxFallSpeed = 0.0;
    /*! Share of the mining speed it adds per level (haste: 0.2 like the reference), in [-1, 10]. */
    double mining = 0.0;
    /*! Extra health per level on top of the hearts while it lasts (absorption: 4); blows take it
        first and it does not grow back; the effect ends when it is used up. Players only. */
    double absorption = 0.0;

    /*! Returns the ticks between two of its hurts at \a level (1 = level I). */
    int periodAt(int level) const { return std::max(1, periodTicks >> std::clamp(level - 1, 0, 30)); }
    /*! Returns whether it acts (hurts or heals, if it does) at \a level in the tick in which
        \a ticksLeft ticks remain. */
    bool actsAt(int level, int ticksLeft) const { return instant || ticksLeft % periodAt(level) == 0; }
    /*! Returns whether it hurts at \a level in the tick in which \a ticksLeft ticks remain. */
    bool hurtsAt(int level, int ticksLeft) const { return damage > 0.0 && actsAt(level, ticksLeft); }
    /*! Returns whether it heals at \a level in the tick in which \a ticksLeft ticks remain. */
    bool healsAt(int level, int ticksLeft) const { return heal > 0.0 && actsAt(level, ticksLeft); }
    /*! Returns \a amount (\c {damage} or \c {heal}) as it acts at \a level: doubled per level above
        I for an instant effect, else as it is. */
    double amountAt(double amount, int level) const { return instant ? amount * (1 << std::clamp(level - 1, 0, 30)) : amount; }
    /*! Returns the factor on a body's speed at \a level. */
    double speedFactorAt(int level) const { return std::max(0.0, 1.0 + speed * std::max(level, 0)); }
    /*! Returns the damage it deals at \a level to a body with \a health, never leaving it below
        \c {minHealth}; 0 when there is no room. */
    double damageTo(double health, int level = 1) const { return std::clamp(health - minHealth, 0.0, amountAt(damage, level)); }
};
