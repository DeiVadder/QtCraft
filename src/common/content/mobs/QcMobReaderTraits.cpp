// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// The traits of creatures that teleport, carry blocks and are provoked by a look (decision E-460):
// "gaze", "teleport", "carries" and "waterDamage"; of companions (decision E-480): "tameable", "shy",
// "packRange", "frightens" and "scaredOfCats"; of water creatures and milking (decisions E-500, E-502):
// "aquatic" and "milk".
#include "common/content/mobs/QcMobReader.h"

#include "common/content/registry/QcContentFields.h"
#include "common/content/registry/QcContentNames.h"

using namespace Qt::StringLiterals;

namespace {

[[nodiscard]] QcGazeDefinition parseGaze(QcJsonFieldReader &reader)
{
    QcGazeDefinition gaze;
    gaze.tolerance = reader.number(u"tolerance"_s, gaze.tolerance, 0.001, 1.0);
    gaze.range = reader.number(u"range"_s, gaze.range, 1.0, 128.0);
    gaze.headShare = reader.number(u"headShare"_s, gaze.headShare, 0.0, 1.0);
    return gaze;
}

[[nodiscard]] QcTeleportDefinition parseTeleport(QcJsonFieldReader &reader, const QString &packNamespace)
{
    QcTeleportDefinition teleport;
    teleport.range = reader.number(u"range"_s, teleport.range, 2.0, 64.0);
    teleport.attempts = reader.integer(u"attempts"_s, teleport.attempts, 1, 64);
    teleport.cooldownSeconds = reader.number(u"cooldownSeconds"_s, teleport.cooldownSeconds, 0.0, 60.0);
    teleport.dodgesProjectiles = reader.boolean(u"dodgesProjectiles"_s, teleport.dodgesProjectiles);
    teleport.whenWet = reader.boolean(u"whenWet"_s, teleport.whenWet);
    teleport.towardsTargetChance = reader.number(u"towardsTargetChance"_s, teleport.towardsTargetChance, 0.0, 1.0);
    teleport.sound = reader.name(u"sound"_s, packNamespace);
    return teleport;
}

[[nodiscard]] QcCarryDefinition parseCarry(QcJsonFieldReader &reader, const QString &packNamespace)
{
    QcCarryDefinition carry;
    carry.blocks = QcContentFields::parseNames(u"blocks"_s, packNamespace, reader);
    carry.takeChance = reader.number(u"takeChance"_s, carry.takeChance, 0.0, 1.0);
    carry.placeChance = reader.number(u"placeChance"_s, carry.placeChance, 0.0, 1.0);
    carry.reach = reader.integer(u"reach"_s, carry.reach, 1, 4);
    if (reader.ok() && (carry.blocks.isEmpty() || carry.blocks.size() > QcCarryDefinition::maxBlocks)) {
        reader.fail(u"'blocks' names 1 to %1 blocks it carries"_s.arg(QcCarryDefinition::maxBlocks));
    }
    return carry;
}

// The collar of a tamed creature: the dyes it can be coloured with, their colours and the first one.
struct Collar
{
    QStringList dyes;
    QList<quint32> colors;
    QString defaultDye;
};

// "collar": {"default": dye, "dyes": {dye: "#rrggbb", ...}}.
[[nodiscard]] Collar parseCollar(const QJsonObject &json, QcJsonFieldReader &reader, const QString &packNamespace)
{
    Collar collar;
    const QJsonObject dyes = json.value(u"dyes"_s).toObject();
    if (dyes.isEmpty() || dyes.size() > QcTameDefinition::maxCollarDyes) {
        reader.fail(u"'dyes' maps 1 to %1 dyes to colours"_s.arg(QcTameDefinition::maxCollarDyes));
        return collar;
    }
    for (auto it = dyes.begin(); it != dyes.end(); ++it) {
        const QString text = it.value().toString();
        bool ok = text.size() == 7 && text.startsWith(u'#');
        const quint32 rgb = ok ? QStringView(text).mid(1).toUInt(&ok, 16) : 0;
        if (!ok) {
            reader.fail(u"the colour of dye '%1' must be \"#rrggbb\""_s.arg(it.key()));
            return collar;
        }
        collar.dyes.append(QcContentNames::qualify(it.key(), packNamespace));
        collar.colors.append(rgb);
    }
    collar.defaultDye = reader.name(u"default"_s, packNamespace);
    if (!collar.dyes.contains(collar.defaultDye)) {
        reader.fail(u"'default' must be one of the 'dyes'"_s);
    }
    return collar;
}

[[nodiscard]] QcTameDefinition parseTame(QcJsonFieldReader &reader, const QString &packNamespace)
{
    QcTameDefinition tame;
    tame.items = QcContentFields::parseNames(u"items"_s, packNamespace, reader);
    tame.chance = reader.number(u"chance"_s, tame.chance, 0.0, 1.0);
    tame.becomes = reader.name(u"becomes"_s, packNamespace);
    tame.healItems = QcContentFields::parseNames(u"healItems"_s, packNamespace, reader);
    tame.healAmount = reader.number(u"healAmount"_s, tame.healAmount, 0.0, 1000.0);
    tame.followDistance = reader.number(u"followDistance"_s, tame.followDistance, 1.0, 32.0);
    tame.stopDistance = reader.number(u"stopDistance"_s, tame.stopDistance, 0.5, 16.0);
    tame.teleportDistance = reader.number(u"teleportDistance"_s, tame.teleportDistance, 4.0, 64.0);
    tame.defends = reader.boolean(u"defends"_s, tame.defends);
    reader.nestedObject(u"collar"_s, [&tame, &packNamespace](const QJsonObject &json, QcJsonFieldReader &nested) {
        Collar collar = parseCollar(json, nested, packNamespace);
        tame.collarDyes = std::move(collar.dyes);
        tame.collarColors = std::move(collar.colors);
        tame.collarDefault = std::move(collar.defaultDye);
    });
    if (reader.ok() && (tame.stopDistance >= tame.followDistance || tame.followDistance >= tame.teleportDistance)) {
        reader.fail(u"'stopDistance' < 'followDistance' < 'teleportDistance'"_s);
    }
    return tame;
}

[[nodiscard]] QcShyDefinition parseShy(QcJsonFieldReader &reader, const QString &packNamespace)
{
    QcShyDefinition shy;
    shy.distance = reader.number(u"distance"_s, shy.distance, 1.0, 32.0);
    shy.calmItems = QcContentFields::parseNames(u"calmItems"_s, packNamespace, reader);
    shy.calmSpeed = reader.number(u"calmSpeed"_s, shy.calmSpeed, 0.0, 10.0);
    return shy;
}

[[nodiscard]] QcAquaticDefinition parseAquatic(QcJsonFieldReader &reader)
{
    QcAquaticDefinition aquatic;
    aquatic.swimSpeed = reader.number(u"swimSpeed"_s, aquatic.swimSpeed, 0.1, 10.0);
    aquatic.airSeconds = reader.number(u"airSeconds"_s, aquatic.airSeconds, 0.0, 600.0);
    aquatic.suffocationDamage = reader.number(u"suffocationDamage"_s, aquatic.suffocationDamage, 0.0, 100.0);
    aquatic.flopSpeed = reader.number(u"flopSpeed"_s, aquatic.flopSpeed, 0.0, 10.0);
    aquatic.schoolRadius = reader.number(u"schoolRadius"_s, aquatic.schoolRadius, 0.0, 16.0);
    aquatic.cohesion = reader.number(u"cohesion"_s, aquatic.cohesion, 0.0, 4.0);
    aquatic.alignment = reader.number(u"alignment"_s, aquatic.alignment, 0.0, 4.0);
    aquatic.separation = reader.number(u"separation"_s, aquatic.separation, 0.0, 4.0);
    aquatic.separationDistance = reader.number(u"separationDistance"_s, aquatic.separationDistance, 0.0, 4.0);
    aquatic.neighbours = reader.integer(u"neighbours"_s, aquatic.neighbours, 0, 32);
    const QString cap = reader.string(u"cap"_s, u"fish"_s);
    if (cap == u"large"_s) {
        aquatic.cap = QcAquaticDefinition::Cap::Large;
    } else if (cap != u"fish"_s) {
        reader.fail(u"'cap' is \"fish\" or \"large\""_s);
    }
    return aquatic;
}

// Swimming in pulses (decision E-974): "pulse" in "aquatic".
[[nodiscard]] QcPulseDefinition parsePulse(QcJsonFieldReader &reader)
{
    QcPulseDefinition pulse;
    pulse.seconds = reader.number(u"seconds"_s, pulse.seconds, 0.2, 10.0);
    pulse.thrust = reader.number(u"thrust"_s, pulse.thrust, 0.05, 0.9);
    pulse.speed = reader.number(u"speed"_s, pulse.speed, 0.1, 10.0);
    pulse.spread = reader.number(u"spread"_s, pulse.spread, 0.0, 90.0);
    return pulse;
}

// Drinking to heal (the witch, decision E-623): the object "drinks".
QcDrinkHealDefinition parseDrinks(QcJsonFieldReader &reader, const QString &packNamespace)
{
    QcDrinkHealDefinition drinks;
    drinks.item = reader.name(u"item"_s, packNamespace);
    drinks.heal = reader.number(u"heal"_s, drinks.heal, 0.5, 1000.0);
    drinks.seconds = reader.number(u"seconds"_s, drinks.seconds, 0.05, 30.0);
    drinks.cooldownSeconds = reader.number(u"cooldownSeconds"_s, drinks.cooldownSeconds, 0.0, 3600.0);
    drinks.sound = reader.name(u"sound"_s, packNamespace);
    if (reader.ok() && drinks.item.isEmpty()) {
        reader.fail(u"needs the 'item' it drinks"_s);
    }
    return drinks;
}

} // namespace

// Drifting, riding, falling apart and the weakness to a projectile (decisions E-1053 to E-1055).
void QcMobReader::parseNetherTraits()
{
    m_reader.nestedObject(u"drift"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        m_mob.drift.seconds = nested.number(u"seconds"_s, 4.0, 0.5, 60.0);
        m_mob.drift.keepsToLava = nested.boolean(u"keepsToLava"_s, false);
    });
    m_reader.nestedObject(u"ride"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        QcRideDefinition ride;
        ride.saddle = nested.name(u"saddle"_s, m_namespace);
        ride.steer = nested.name(u"steer"_s, m_namespace);
        ride.speed = nested.number(u"speed"_s, 3.0, 0.1, 20.0);
        ride.seat = nested.number(u"seat"_s, 1.2, 0.0, 8.0);
        m_mob.ride = std::move(ride);
    });
    m_reader.nestedObject(u"splits"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        QcSplitDefinition splits;
        splits.into = nested.name(u"into"_s, m_namespace);
        splits.min = nested.integer(u"min"_s, 2, 1, 8);
        splits.max = nested.integer(u"max"_s, 4, splits.min, 8);
        m_mob.splits = std::move(splits);
    });
    m_reader.nestedObject(u"weakTo"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        m_mob.weakTo = nested.name(u"projectile"_s, m_namespace);
        m_mob.deflectedDamage = nested.number(u"damage"_s, 1000.0, 0.0, 10000.0);
    });
}

// Taming, shyness, packs and cats (decision E-480).
void QcMobReader::parseCompanions()
{
    m_reader.nestedObject(u"tameable"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        m_mob.tameable = parseTame(nested, m_namespace);
    });
    m_reader.nestedObject(u"shy"_s,
                        [this](const QJsonObject &, QcJsonFieldReader &nested) { m_mob.shy = parseShy(nested, m_namespace); });
    m_mob.packRange = m_reader.number(u"packRange"_s, 0.0, 0.0, 64.0);
    m_mob.fireImmune = m_reader.boolean(u"fireImmune"_s, false);
    m_mob.hops = m_reader.boolean(u"hops"_s, false);
    parseNetherTraits();
    m_mob.frightens = m_reader.boolean(u"frightens"_s, false);
    m_mob.scaredOfCats = m_reader.boolean(u"scaredOfCats"_s, false);

    if (m_reader.ok() && m_mob.packRange > 0.0 && m_mob.ai != QcMobDefinition::Ai::Neutral) {
        m_reader.fail(u"'packRange' goes with the neutral AI"_s);
    }
    const bool walks = m_mob.ai == QcMobDefinition::Ai::Passive || m_mob.ai == QcMobDefinition::Ai::Neutral;
    if (m_reader.ok() && (m_mob.tameable || m_mob.shy) && (!walks || m_mob.flying || m_mob.hostile)) {
        m_reader.fail(u"'tameable' and 'shy' go with the passive or neutral AI of a creature that walks and is not hostile"_s);
    }
}

// Water creatures and milking (decisions E-500, E-502): "aquatic" (with "pulse" and "ink",
// decision E-974) and "milk".
void QcMobReader::parseWaterAndMilk()
{
    m_reader.nestedObject(u"aquatic"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        m_mob.aquatic = parseAquatic(nested);
        nested.nestedObject(u"pulse"_s, [this](const QJsonObject &, QcJsonFieldReader &pulse) { m_mob.pulse = parsePulse(pulse); });
        m_mob.ink = nested.name(u"ink"_s, m_namespace);
    });
    if (m_reader.ok() && m_mob.aquatic && (m_mob.flying || m_mob.hostile || m_mob.ambient || m_mob.ai != QcMobDefinition::Ai::Passive || m_mob.tameable)) {
        m_reader.fail(u"'aquatic' creatures are passive, neither fly, are ambient nor tameable"_s);
    }
    m_reader.nestedObject(u"milk"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        m_mob.milkWith = nested.name(u"with"_s, m_namespace);
        m_mob.milkItem = nested.name(u"item"_s, m_namespace);
        m_mob.milkSound = nested.name(u"sound"_s, m_namespace);
        if (nested.ok() && (m_mob.milkWith.isEmpty() || m_mob.milkItem.isEmpty())) {
            nested.fail(u"needs 'with' and 'item'"_s);
        }
    });
}

// Crumbling at a liquid's source and guarding a zone (decisions E-593, E-594): "crumbles" and
// "guardian".
void QcMobReader::parseGuardian()
{
    m_reader.nestedObject(u"crumbles"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        QcCrumbleDefinition &crumbles = m_mob.crumbles.emplace();
        crumbles.liquid = nested.name(u"liquid"_s, m_namespace);
        crumbles.body = nested.name(u"body"_s, m_namespace);
        crumbles.head = nested.name(u"head"_s, m_namespace);
        crumbles.sound = nested.name(u"sound"_s, m_namespace);
        if (nested.ok() && (crumbles.liquid.isEmpty() || crumbles.body.isEmpty() || crumbles.head.isEmpty())) {
            nested.fail(u"needs a 'liquid', a 'body' and a 'head' block"_s);
        }
    });
    m_reader.nestedObject(u"guardian"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        m_mob.guardLeash = nested.number(u"leash"_s, m_mob.guardLeash, 0.0, 32.0);
    });
    if (m_reader.ok() && m_mob.crumbles && (m_mob.flying || m_mob.aquatic)) {
        m_reader.fail(u"'crumbles' goes with creatures that walk on land"_s);
    }
}

void QcMobReader::parseCreatureTraits()
{
    m_reader.nestedObject(u"gaze"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) { m_mob.gaze = parseGaze(nested); });
    m_reader.nestedObject(u"teleport"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        m_mob.teleport = parseTeleport(nested, m_namespace);
    });
    m_reader.nestedObject(u"carries"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        m_mob.carries = parseCarry(nested, m_namespace);
    });
    m_mob.waterDamage = m_reader.number(u"waterDamage"_s, 0.0, 0.0, 100.0);
    parseCompanions();
    parseWaterAndMilk();
    parseGuardian();
    m_reader.nestedObject(u"drinks"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        m_mob.drinks = parseDrinks(nested, m_namespace);
    });

    // Being provoked by a look and carrying blocks are what the neutral AI does.
    if (m_reader.ok() && (m_json.contains(u"gaze"_s) || m_json.contains(u"carries"_s)) && m_mob.ai != QcMobDefinition::Ai::Neutral) {
        m_reader.fail(u"'gaze' and 'carries' go with the neutral AI"_s);
    }
    if (m_reader.ok() && m_mob.drinks && m_mob.ai != QcMobDefinition::Ai::Archer) {
        m_reader.fail(u"'drinks' goes with the archer AI"_s);
    }
    if (m_reader.ok() && m_mob.flying && (m_mob.teleport || m_mob.carries)) {
        m_reader.fail(u"flying creatures neither teleport nor carry blocks"_s);
    }
}
