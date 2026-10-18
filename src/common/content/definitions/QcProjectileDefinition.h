#pragma once

#include "common/content/definitions/QcHitEffect.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <algorithm>

// A kind of projectile (decision E-425), from projectiles/<name>.json; server side only. Its flight
// is ballistic: gravity pulls it down and drag takes a share of its speed every second (more in
// water). A hit on a body does the effects of `onHit` (decision E-427; the arrow: damage per 20 m/s
// of speed, the reference's one block per tick, rounded up, and knockback); a hit on a block sticks
// it there when `sticks` and it does not break, where a player can pick it up again as its `item`
// until it despawns.
struct QcProjectileDefinition
{
    // Fastest launch any launcher may give (m/s); the flight's sub-steps are sized for it.
    static constexpr double maxSpeed = 200.0;
    // The speed a hit of `damage` is measured at.
    static constexpr double damageSpeed = 20.0;

    QString name;
    // The item it is shot from (ammunition), drawn as and given back when picked up; also the look
    // the client draws (QcMsg::EntitySpawn::item).
    QString item;
    // m/s² downwards.
    double gravity = 20.0;
    // Share of the speed lost per second, as the rate of an exponential decay (0.2: about 1 % per
    // tick like the reference's arrows), in air and in water.
    double drag = 0.2;
    double waterDrag = 10.0;
    // Health points per damageSpeed of speed: the sum of the damage effects of onHit (0 without one).
    double damage = 2.0;
    // What a hit does, in order (decision E-427). Without "onHit" in the file: damage (`damage`)
    // and knockback, the arrow's.
    QList<QcHitEffect> onHit;
    // Edge of the box that hits bodies (m); blocks stop its centre.
    double size = 0.5;
    // Seconds it stays stuck in a block before it goes, and seconds it may fly at most.
    double despawnSeconds = 60.0;
    double flightSeconds = 60.0;
    bool sticks = true;
    // Sound keys: hitting a body, sticking in a block.
    QString hitSound;
    QString stickSound;

    // The first effect of `type` in onHit, if any.
    const QcHitEffect *effect(const char *type) const
    {
        const auto it = std::ranges::find_if(onHit, [type](const QcHitEffect &effect) { return effect.is(type); });
        return it == onHit.end() ? nullptr : &*it;
    }
    // Whether it goes at a hit instead of bouncing off a body or sticking in a block; one that
    // teleports its thrower always goes where it lands (decision E-460).
    bool breaks() const { return effect(QcHitEffect::breaks) != nullptr || effect(QcHitEffect::teleport) != nullptr; }
};

// What an item that shoots projectiles does (a bow, decision E-425): held with the use button down,
// it draws for up to drawSeconds; released, it shoots the first ammunition of the inventory it
// takes (decision E-427) at `speed` times the power of the draw, if the power reached minPower.
struct QcLauncherDefinition
{
    double drawSeconds = 1.0;
    double speed = 60.0;
    double minPower = 0.1;
    // What it shoots (decision E-427): item names and item tags as "#tag" (the bow: "#qtcraft:arrows");
    // empty for every ammunition item that is not throwable (QcThrowableDefinition).
    QStringList ammunition;
    // The file wrote an empty list ("ammunition": []), which means the same as none; the content's
    // finish warns and clears it (QcContentRegistry::resolveProjectiles).
    bool emptyAmmunitionList = false;

    // Whether the launcher takes the item `name` with `tags` as ammunition, by its list; an empty
    // list takes everything (the caller leaves out throwable items).
    bool takes(const QString &name, const QStringList &tags) const
    {
        return ammunition.isEmpty() || std::ranges::any_of(ammunition, [&](const QString &entry) {
                   return entry.startsWith(u'#') ? tags.contains(entry.mid(1)) : entry == name;
               });
    }

    // The power (0..1) of a draw of `seconds`: the reference's curve (f² + 2f) / 3 of the share f of
    // drawSeconds. Server and client (the animation) use the same.
    double powerAfter(double seconds) const
    {
        const double share = std::clamp(seconds / drawSeconds, 0.0, 1.0);
        return std::min(1.0, (share * share + 2.0 * share) / 3.0);
    }

    bool operator==(const QcLauncherDefinition &) const = default;
};

// An item thrown by hand as its own projectile (a snowball, decision E-427): the use button throws
// one at once at `speed`, randomly turned by up to inaccuracyDegrees; the thrower waits
// cooldownSeconds before the next throw of that item. Reaches the client (arm swing, cooldown).
struct QcThrowableDefinition
{
    double speed = 30.0;
    double cooldownSeconds = 0.25;
    double inaccuracyDegrees = 1.0;

    bool operator==(const QcThrowableDefinition &) const = default;
};
