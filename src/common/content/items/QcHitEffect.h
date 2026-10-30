// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonObject>
#include <QString>

#include <array>
#include <optional>

/*! \brief One thing a projectile does when it hits (decision E-427), an entry of its kind's "onHit"
    list: {"type": "knockback", "strength": 0.5}.

    The types are a table in code (QcHitEffect::types), each with one number of its own; the
    server applies them in QcHitEffects. A type no table row names is left out of the list with a
    warning when the content is finished. */
struct QcHitEffect
{
    /*! \brief A type of effect: its name, the key and range of its number and the number when
        not given. */
    struct Type
    {
        /*! The type's name, the value of \c {"type"} in data. */
        const char *name;
        /*! Key of the type's number in the entry; empty for a type without a number. */
        const char *numberKey;
        /*! Smallest value the number may have. */
        double low;
        /*! Largest value the number may have. */
        double high;
        /*! The number when the entry gives none (the damage type takes the kind's
            \c {damage} instead). */
        double fallback;
    };

    /*! Damage to the body hit: \c {amount} health points per QcProjectileDefinition::damageSpeed
        of speed, rounded up; the body is pushed only with a knockback effect. */
    static constexpr const char *damage = "damage";
    /*! Pushes the body hit along the flight like a blow (\c {strength} 1), also without damage. */
    static constexpr const char *knockback = "knockback";
    /*! Sets the body hit burning for \c {seconds} (as fire does, decision E-393). */
    static constexpr const char *ignite = "ignite";
    /*! The projectile goes when it hits a body or a block instead of bouncing or sticking;
        \c {sound} plays where it breaks. */
    static constexpr const char *breaks = "break";
    /*! Gives the body hit the status effect \c {effect} of \c {level} for \c {seconds} (decision
        E-450), only when the hit counts (not in the short invulnerability). */
    static constexpr const char *effect = "effect";
    /*! Where a breaking projectile breaks (at a body or a block), with the chance \c {chance}
        the creatures \c {mob} appear there as young ones: \c {count} of them, or with
        \c {groupChance} of those times \c {groupCount} (an egg's chicks, decision E-452). */
    static constexpr const char *spawn = "spawn";
    /*! Teleports the player who threw it to where it lands, on a body or a block (a shade pearl,
        decision E-460), with \c {damage} as fall damage; the projectile breaks there. */
    static constexpr const char *teleport = "teleport";
    /*! Where the projectile breaks, its damage (the \c {amount} of its damage effects, not per
        speed) and status effects reach every living body within \c {radius} metres but its
        thrower, less with the distance (a splash potion, decision E-624); the body hit takes
        nothing else. */
    static constexpr const char *splash = "splash";

    /*! The known types, one row each, in the order of the names above. */
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

    /*! Returns the row of \c {types} named \a name, or std::nullopt for an unknown type. */
    static std::optional<Type> typeNamed(const QString &name)
    {
        for (const Type &type : types) {
            if (name == QLatin1StringView(type.name)) {
                return type;
            }
        }
        return std::nullopt;
    }

    /*! The type's name as written (\c {"type"}, required); it may name no known type. */
    QString type;
    /*! The type's number (Type::numberKey), its fallback when not given. */
    double number = 0.0;
    /*! A sound key (break). */
    QString sound;
    /*! The entry as written, for types added in code that read more of it. */
    QJsonObject fields;
    /*! The kind named: the status effect (effect) or the creature (spawn). */
    QString subject;
    /*! The effect's level (effect). */
    int level = 1;
    /*! How many appear (spawn; \c {"count"}, 1 to 16). */
    int count = 1;
    /*! Chance that \c {groupCount} appear instead (spawn; \c {"groupChance"}, 0 to 1). */
    double groupChance = 0.0;
    /*! How many appear in the bigger group (spawn; \c {"groupCount"}, 1 to 16). */
    int groupCount = 4;

    /*! Returns whether this entry's type is \a name, one of the type names above. */
    bool is(const char *name) const { return type == QLatin1StringView(name); }
};
