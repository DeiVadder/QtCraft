#pragma once

#include "common/gameplay/QcStatusEffects.h"

#include <QStringView>

// How long one arm swing lasts (decision E-881), shared by the client's animation and the server's
// cadence of swings while a player mines: the reference's six ticks (300 ms), one tick shorter per
// level of a hastening effect and two ticks longer per level of a tiring one. Everything that
// changes the swing (status effects, a beacon, an enchantment of our own) adds its levels or ticks
// here, so the animation only ever asks ticks().
struct QcSwingSpeed
{
    // The reference's swing without effects.
    static constexpr int baseTicks = 6;
    static constexpr int minTicks = 1;
    // Mining fatigue V would be 16; the bound keeps a mod's level 100 from freezing the arm.
    static constexpr int maxTicks = 40;
    // The status effects that change the swing, named as in the reference: QcStatusEffect::name
    // without its namespace, so any pack's "haste" counts.
    static constexpr QStringView hasteEffect = u"haste";
    static constexpr QStringView fatigueEffect = u"mining_fatigue";

    // Levels that shorten the swing by one tick each (haste, a beacon's haste).
    int faster = 0;
    // Levels that lengthen it by two ticks each (mining fatigue).
    int slower = 0;
    // Any other change in ticks, negative for faster (an enchantment of our own).
    int extraTicks = 0;

    // The swing's length in ticks, within minTicks..maxTicks.
    [[nodiscard]] int ticks() const;

    // The swing of a body with these status effects: haste and mining fatigue by their names, so a
    // pack that defines them changes the swing without any code (none of the base content does).
    [[nodiscard]] static QcSwingSpeed of(const QList<QcStatusEffect> &effects);

    bool operator==(const QcSwingSpeed &) const = default;
};
