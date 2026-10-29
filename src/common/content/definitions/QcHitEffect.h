// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonObject>
#include <QString>

#include <array>
#include <optional>

// One thing a projectile does when it hits (decision E-427), an entry of its kind's "onHit" list:
// {"type": "knockback", "strength": 0.5}. The types are a table in code (QcHitEffect::types), each
// with one number of its own; the server applies them in QcHitEffects. A type no table row names is
// left out of the list with a warning when the content is finished.
struct QcHitEffect
{
    // A type of effect: its name, the key and range of its number and the number when not given.
    struct Type
    {
        const char *name;
        // Empty for a type without a number.
        const char *numberKey;
        double low;
        double high;
        double fallback;
    };

    // Damage to the body hit: `amount` health points per QcProjectileDefinition::damageSpeed of
    // speed, rounded up; the body is pushed only with a knockback effect.
    static constexpr const char *damage = "damage";
    // Pushes the body hit along the flight like a blow (`strength` 1), also without damage.
    static constexpr const char *knockback = "knockback";
    // Sets the body hit burning for `seconds` (as fire does, decision E-393).
    static constexpr const char *ignite = "ignite";
    // The projectile goes when it hits a body or a block instead of bouncing or sticking; `sound`
    // plays where it breaks.
    static constexpr const char *breaks = "break";
    // Gives the body hit the status effect `effect` of `level` for `seconds` (decision E-450), only
    // when the hit counts (not in the short invulnerability).
    static constexpr const char *effect = "effect";
    // Where a breaking projectile breaks (at a body or a block), with the chance `chance` the
    // creatures `mob` appear there as young ones: `count` of them, or with `groupChance` of those
    // times `groupCount` (an egg's chicks, decision E-452).
    static constexpr const char *spawn = "spawn";
    // Teleports the player who threw it to where it lands, on a body or a block (a shade pearl,
    // decision E-460), with `damage` as fall damage; the projectile breaks there.
    static constexpr const char *teleport = "teleport";
    // Where the projectile breaks, its damage (the `amount` of its damage effects, not per speed)
    // and status effects reach every living body within `radius` metres but its thrower, less with
    // the distance (a splash potion, decision E-624); the body hit takes nothing else.
    static constexpr const char *splash = "splash";

    static constexpr std::array<Type, 8> types{{
        {damage, "amount", 0.0, 1000.0, 2.0},
        {knockback, "strength", 0.0, 10.0, 1.0},
        {ignite, "seconds", 0.0, 3600.0, 5.0},
        {breaks, "", 0.0, 0.0, 0.0},
        {effect, "seconds", 0.05, 3600.0, 5.0},
        {spawn, "chance", 0.0, 1.0, 1.0},
        {teleport, "damage", 0.0, 1000.0, 5.0},
        {splash, "radius", 0.5, 8.0, 4.0},
    }};

    static std::optional<Type> typeNamed(const QString &name)
    {
        for (const Type &type : types) {
            if (name == QLatin1StringView(type.name)) {
                return type;
            }
        }
        return std::nullopt;
    }

    QString type;
    // The type's number (Type::numberKey), its fallback when not given.
    double number = 0.0;
    // A sound key (break).
    QString sound;
    // The entry as written, for types added in code that read more of it.
    QJsonObject fields;
    // The kind named: the status effect (effect) or the creature (spawn).
    QString subject;
    // The effect's level (effect).
    int level = 1;
    // How many appear, and the chance and size of a bigger group (spawn).
    int count = 1;
    double groupChance = 0.0;
    int groupCount = 4;

    bool is(const char *name) const { return type == QLatin1StringView(name); }
};
