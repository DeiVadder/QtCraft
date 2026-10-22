#pragma once

#include <QString>
#include <QtGlobal>

#include <algorithm>

// A kind of status effect (decision E-450), from effects/<name>.json: what it does to a body while
// it lasts (poison: damage now and then, never below `minHealth`) and how the HUD shows it (icon,
// colour, hearts in the colour). Players and creatures carry effects of these kinds
// (QcStatusEffects); the content sync sends every field but `exhaustion`: the HUD needs the looks,
// the client's prediction the speed.
struct QcEffectDefinition
{
    // The highest level an effect may have (/effect, onHit "effect", saved effects).
    static constexpr int maxLevel = 10;

    QString name;
    QString translationKey;
    // Item whose picture the HUD shows beside the remaining time; empty for a plain badge.
    QString icon;
    // 0xRRGGBB of the badge and, with `tintsHearts`, of the hearts while it lasts.
    quint32 colour = 0xffffff;
    bool tintsHearts = false;
    // Damage dealt every `periodTicks` ticks at level 1; each level above halves the period (the
    // reference's 25 >> (level - 1) for poison), at least one tick.
    double damage = 0.0;
    int periodTicks = 25;
    // Its damage never takes the health below this (poison: 1, it never kills).
    double minHealth = 0.0;
    // Share of the speed it adds per level, in [-1, 1] (slowness: -0.15 like the reference, decision
    // E-838): walking, swimming and chasing bodies move at 1 + speed × level of their speed, at
    // least 0. Flying players are not slowed.
    double speed = 0.0;
    // Food exhaustion per second at level I (hunger: 0.1 like the reference's 0.005 per tick,
    // decision E-838), times the level; players only. Server side only.
    double exhaustion = 0.0;

    // The ticks between two of its hurts at `level` (1 = level I).
    int periodAt(int level) const { return std::max(1, periodTicks >> std::clamp(level - 1, 0, 30)); }
    // Whether it hurts in the tick in which `ticksLeft` ticks remain.
    bool hurtsAt(int level, int ticksLeft) const { return damage > 0.0 && ticksLeft % periodAt(level) == 0; }
    // The factor on a body's speed at `level`.
    double speedFactorAt(int level) const { return std::max(0.0, 1.0 + speed * std::max(level, 0)); }
    // The damage it deals to a body with `health`, kept above `minHealth`; 0 when there is no room.
    double damageTo(double health) const { return std::clamp(health - minHealth, 0.0, damage); }
};
