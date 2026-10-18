#include "common/content/parser/QcMobTraitsParser.h"

#include "common/content/parser/QcContentFields.h"

using namespace Qt::StringLiterals;

namespace {

void parseGaze(QcGazeDefinition &gaze, QcJsonFieldReader &reader)
{
    gaze.tolerance = reader.number(u"tolerance"_s, gaze.tolerance, 0.001, 1.0);
    gaze.range = reader.number(u"range"_s, gaze.range, 1.0, 128.0);
    gaze.headShare = reader.number(u"headShare"_s, gaze.headShare, 0.0, 1.0);
}

void parseTeleport(QcTeleportDefinition &teleport, QcJsonFieldReader &reader)
{
    teleport.range = reader.number(u"range"_s, teleport.range, 2.0, 64.0);
    teleport.attempts = reader.integer(u"attempts"_s, teleport.attempts, 1, 64);
    teleport.cooldownSeconds = reader.number(u"cooldownSeconds"_s, teleport.cooldownSeconds, 0.0, 60.0);
    teleport.dodgesProjectiles = reader.boolean(u"dodgesProjectiles"_s, teleport.dodgesProjectiles);
    teleport.whenWet = reader.boolean(u"whenWet"_s, teleport.whenWet);
    teleport.towardsTargetChance = reader.number(u"towardsTargetChance"_s, teleport.towardsTargetChance, 0.0, 1.0);
    teleport.sound = reader.string(u"sound"_s);
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

} // namespace

namespace QcMobTraitsParser {

void parseTraits(const QJsonObject &json, QcMobDefinition &mob, QcJsonFieldReader &reader, const QString &packNamespace)
{
    reader.nestedObject(u"gaze"_s, [&mob](const QJsonObject &, QcJsonFieldReader &nested) { parseGaze(mob.gaze.emplace(), nested); });
    reader.nestedObject(u"teleport"_s,
                        [&mob](const QJsonObject &, QcJsonFieldReader &nested) { parseTeleport(mob.teleport.emplace(), nested); });
    reader.nestedObject(u"carries"_s, [&mob, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) {
        parseCarry(mob.carries.emplace(), nested, packNamespace);
    });
    mob.waterDamage = reader.number(u"waterDamage"_s, 0.0, 0.0, 100.0);

    // Being provoked by a look and carrying blocks are what the neutral AI does.
    if (reader.ok() && (json.contains(u"gaze"_s) || json.contains(u"carries"_s)) && mob.ai != QcMobDefinition::Ai::Neutral) {
        reader.fail(u"'gaze' and 'carries' go with the neutral AI"_s);
    }
    if (reader.ok() && mob.flying && (mob.teleport || mob.carries)) {
        reader.fail(u"flying creatures neither teleport nor carry blocks"_s);
    }
}

} // namespace QcMobTraitsParser
