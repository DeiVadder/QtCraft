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

    static constexpr std::array<Type, 4> types{{
        {damage, "amount", 0.0, 1000.0, 2.0},
        {knockback, "strength", 0.0, 10.0, 1.0},
        {ignite, "seconds", 0.0, 3600.0, 5.0},
        {breaks, "", 0.0, 0.0, 0.0},
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

    bool is(const char *name) const { return type == QLatin1StringView(name); }
};
