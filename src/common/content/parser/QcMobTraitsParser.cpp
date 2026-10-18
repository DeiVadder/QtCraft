#include "common/content/parser/QcMobTraitsParser.h"

#include "common/content/parser/QcContentFields.h"
#include "common/content/registry/QcContentNames.h"

using namespace Qt::StringLiterals;

namespace {

void parseGaze(QcGazeDefinition &gaze, QcJsonFieldReader &reader)
{
    gaze.tolerance = reader.number(u"tolerance"_s, gaze.tolerance, 0.001, 1.0);
    gaze.range = reader.number(u"range"_s, gaze.range, 1.0, 128.0);
    gaze.headShare = reader.number(u"headShare"_s, gaze.headShare, 0.0, 1.0);
}

void parseTeleport(QcTeleportDefinition &teleport, QcJsonFieldReader &reader, const QString &packNamespace)
{
    teleport.range = reader.number(u"range"_s, teleport.range, 2.0, 64.0);
    teleport.attempts = reader.integer(u"attempts"_s, teleport.attempts, 1, 64);
    teleport.cooldownSeconds = reader.number(u"cooldownSeconds"_s, teleport.cooldownSeconds, 0.0, 60.0);
    teleport.dodgesProjectiles = reader.boolean(u"dodgesProjectiles"_s, teleport.dodgesProjectiles);
    teleport.whenWet = reader.boolean(u"whenWet"_s, teleport.whenWet);
    teleport.towardsTargetChance = reader.number(u"towardsTargetChance"_s, teleport.towardsTargetChance, 0.0, 1.0);
    teleport.sound = reader.name(u"sound"_s, packNamespace);
}

void parseCarry(QcCarryDefinition &carry, QcJsonFieldReader &reader, const QString &packNamespace)
{
    carry.blocks = QcContentFields::parseNames(u"blocks"_s, packNamespace, reader);
    carry.takeChance = reader.number(u"takeChance"_s, carry.takeChance, 0.0, 1.0);
    carry.placeChance = reader.number(u"placeChance"_s, carry.placeChance, 0.0, 1.0);
    carry.reach = reader.integer(u"reach"_s, carry.reach, 1, 4);
    if (reader.ok() && carry.blocks.isEmpty()) {
        reader.fail(u"'blocks' names the blocks it carries"_s);
    }
}

// "collar": {"default": dye, "dyes": {dye: "#rrggbb", ...}}.
void parseCollar(QcTameDefinition &tame, const QJsonObject &json, QcJsonFieldReader &reader, const QString &packNamespace)
{
    const QJsonObject dyes = json.value(u"dyes"_s).toObject();
    if (dyes.isEmpty() || dyes.size() > QcTameDefinition::maxItems) {
        reader.fail(u"'dyes' maps 1 to %1 dyes to colours"_s.arg(QcTameDefinition::maxItems));
        return;
    }
    for (auto it = dyes.begin(); it != dyes.end(); ++it) {
        const QString text = it.value().toString();
        bool ok = text.size() == 7 && text.startsWith(u'#');
        const quint32 rgb = ok ? QStringView(text).mid(1).toUInt(&ok, 16) : 0;
        if (!ok) {
            reader.fail(u"the colour of dye '%1' must be \"#rrggbb\""_s.arg(it.key()));
            return;
        }
        tame.collarDyes.append(QcContentNames::qualify(it.key(), packNamespace));
        tame.collarColors.append(rgb);
    }
    tame.collarDefault = reader.name(u"default"_s, packNamespace);
    if (!tame.collarDyes.contains(tame.collarDefault)) {
        reader.fail(u"'default' must be one of the 'dyes'"_s);
    }
}

void parseTame(QcTameDefinition &tame, QcJsonFieldReader &reader, const QString &packNamespace)
{
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
        parseCollar(tame, json, nested, packNamespace);
    });
    if (reader.ok() && (tame.stopDistance >= tame.followDistance || tame.followDistance >= tame.teleportDistance)) {
        reader.fail(u"'stopDistance' < 'followDistance' < 'teleportDistance'"_s);
    }
}

void parseShy(QcShyDefinition &shy, QcJsonFieldReader &reader, const QString &packNamespace)
{
    shy.distance = reader.number(u"distance"_s, shy.distance, 1.0, 32.0);
    shy.calmItems = QcContentFields::parseNames(u"calmItems"_s, packNamespace, reader);
    shy.calmSpeed = reader.number(u"calmSpeed"_s, shy.calmSpeed, 0.0, 10.0);
}

// Taming, shyness, packs and cats (decision E-480).
void parseCompanions(QcMobDefinition &mob, QcJsonFieldReader &reader, const QString &packNamespace)
{
    reader.nestedObject(u"tameable"_s, [&mob, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) {
        parseTame(mob.tameable.emplace(), nested, packNamespace);
    });
    reader.nestedObject(u"shy"_s,
                        [&mob, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) { parseShy(mob.shy.emplace(), nested, packNamespace); });
    mob.packRange = reader.number(u"packRange"_s, 0.0, 0.0, 64.0);
    mob.frightens = reader.boolean(u"frightens"_s, false);
    mob.scaredOfCats = reader.boolean(u"scaredOfCats"_s, false);

    if (reader.ok() && mob.packRange > 0.0 && mob.ai != QcMobDefinition::Ai::Neutral) {
        reader.fail(u"'packRange' goes with the neutral AI"_s);
    }
    const bool walks = mob.ai == QcMobDefinition::Ai::Passive || mob.ai == QcMobDefinition::Ai::Neutral;
    if (reader.ok() && (mob.tameable || mob.shy) && (!walks || mob.flying || mob.hostile)) {
        reader.fail(u"'tameable' and 'shy' go with the passive or neutral AI of a creature that walks and is not hostile"_s);
    }
}

void parseAquatic(QcAquaticDefinition &aquatic, QcJsonFieldReader &reader)
{
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
}

// Water creatures and milking (decisions E-500, E-502): "aquatic" and "milk".
void parseWaterAndMilk(QcMobDefinition &mob, QcJsonFieldReader &reader, const QString &packNamespace)
{
    reader.nestedObject(u"aquatic"_s, [&mob](const QJsonObject &, QcJsonFieldReader &nested) { parseAquatic(mob.aquatic.emplace(), nested); });
    if (reader.ok() && mob.aquatic && (mob.flying || mob.hostile || mob.ambient || mob.ai != QcMobDefinition::Ai::Passive || mob.tameable)) {
        reader.fail(u"'aquatic' creatures are passive, neither fly, are ambient nor tameable"_s);
    }
    reader.nestedObject(u"milk"_s, [&mob, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) {
        mob.milkWith = nested.name(u"with"_s, packNamespace);
        mob.milkItem = nested.name(u"item"_s, packNamespace);
        mob.milkSound = nested.name(u"sound"_s, packNamespace);
        if (nested.ok() && (mob.milkWith.isEmpty() || mob.milkItem.isEmpty())) {
            nested.fail(u"needs 'with' and 'item'"_s);
        }
    });
}

} // namespace

namespace QcMobTraitsParser {

void parseTraits(const QJsonObject &json, QcMobDefinition &mob, QcJsonFieldReader &reader, const QString &packNamespace)
{
    reader.nestedObject(u"gaze"_s, [&mob](const QJsonObject &, QcJsonFieldReader &nested) { parseGaze(mob.gaze.emplace(), nested); });
    reader.nestedObject(u"teleport"_s,
                        [&mob, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) { parseTeleport(mob.teleport.emplace(), nested, packNamespace); });
    reader.nestedObject(u"carries"_s, [&mob, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) {
        parseCarry(mob.carries.emplace(), nested, packNamespace);
    });
    mob.waterDamage = reader.number(u"waterDamage"_s, 0.0, 0.0, 100.0);
    parseCompanions(mob, reader, packNamespace);
    parseWaterAndMilk(mob, reader, packNamespace);

    // Being provoked by a look and carrying blocks are what the neutral AI does.
    if (reader.ok() && (json.contains(u"gaze"_s) || json.contains(u"carries"_s)) && mob.ai != QcMobDefinition::Ai::Neutral) {
        reader.fail(u"'gaze' and 'carries' go with the neutral AI"_s);
    }
    if (reader.ok() && mob.flying && (mob.teleport || mob.carries)) {
        reader.fail(u"flying creatures neither teleport nor carry blocks"_s);
    }
}

} // namespace QcMobTraitsParser
