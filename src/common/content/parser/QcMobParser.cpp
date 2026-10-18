#include "common/content/parser/QcMobParser.h"

#include "common/content/definitions/QcEffectDefinition.h"
#include "common/content/definitions/QcSoundKeys.h"
#include "common/content/parser/QcContentFields.h"
#include "common/content/parser/QcJsonFieldReader.h"
#include "common/content/parser/QcMobTraitsParser.h"
#include "common/content/registry/QcContentNames.h"
#include "common/core/QcWorldConstants.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

QList<QcModelBox> parseBoxes(const QJsonValue &value, int limit, QcJsonFieldReader &reader)
{
    QList<QcModelBox> boxes;
    if (!value.isArray() || value.toArray().size() > limit) {
        reader.fail(u"'boxes' must be an array of at most %1 boxes"_s.arg(limit));
        return boxes;
    }

    const QJsonArray list = value.toArray();
    for (const auto &entry : list) {
        const QJsonObject boxJson = entry.toObject();
        const std::optional<QcVec3> from = QcContentFields::vectorField(boxJson.value(u"from"_s));
        const std::optional<QcVec3> to = QcContentFields::vectorField(boxJson.value(u"to"_s));
        const QString slot = boxJson.value(u"slot"_s).toString();
        if (!from || !to || slot.isEmpty() || slot.size() > 64 || from->x >= to->x || from->y >= to->y || from->z >= to->z) {
            reader.fail(u"boxes need 'from' < 'to' (three numbers within 8 m) and a 'slot'"_s);
            return {};
        }
        boxes.append({*from, *to, slot});
    }
    return boxes;
}

// The tint and the sitting offset of a model part (decision E-480): "tint" none or collar,
// "sitOffset" three numbers within 1 m.
[[nodiscard]] std::optional<QcMobPart> partLook(QcMobPart part, const QJsonObject &json, QcJsonFieldReader &reader)
{
    const QString tint = json.value(u"tint"_s).toString(u"none"_s);
    if (tint != u"none"_s && tint != u"collar"_s) {
        reader.fail(u"a model part's 'tint' must be none or collar"_s);
        return std::nullopt;
    }
    part.tint = tint == u"collar"_s ? QcTint::Collar : QcTint::None;
    if (json.contains(u"sitOffset"_s)) {
        const std::optional<QcVec3> offset = QcContentFields::vectorField(json.value(u"sitOffset"_s));
        if (!offset || offset->length() > 1.0) {
            reader.fail(u"a model part's 'sitOffset' must be three numbers within 1 m"_s);
            return std::nullopt;
        }
        part.sitOffset = *offset;
    }
    return part;
}

[[nodiscard]] std::optional<QcTint> tintFromName(const QString &name)
{
    static const QHash<QString, QcTint> tints = {
        {u"none"_s, QcTint::None}, {u"skin"_s, QcTint::Skin}, {u"hair"_s, QcTint::Hair},
        {u"shirt"_s, QcTint::Shirt}, {u"pants"_s, QcTint::Pants},
    };
    const auto it = tints.constFind(name);
    return it == tints.constEnd() ? std::nullopt : std::optional<QcTint>(*it);
}

QList<QcItemDrop> parseDrops(const QJsonValue &drops, const QString &packNamespace, QcJsonFieldReader &reader)
{
    QList<QcItemDrop> result;
    if (drops.isUndefined()) {
        return result;
    }
    if (!drops.isArray()) {
        reader.fail(u"'drops' must be an array"_s);
        return result;
    }

    const QJsonArray list = drops.toArray();
    for (const auto &entry : list) {
        const QJsonObject dropJson = entry.toObject();
        QcJsonFieldReader dropReader{dropJson, reader.context, {}};
        const QcItemDrop drop{dropReader.name(u"item"_s, packNamespace), dropReader.integer(u"count"_s, 1, 1, 64),
                              dropReader.number(u"chance"_s, 1.0, 0.0, 1.0)};
        if (!entry.isObject() || !dropReader.ok() || drop.item.isEmpty()) {
            reader.fail(dropReader.ok() ? u"drop needs an 'item'"_s : dropReader.error);
            break;
        }
        result.append(drop);
    }
    return result;
}

void parseMobStats(QcMobDefinition &mob, QcJsonFieldReader &reader)
{
    mob.maxHealth = reader.number(u"health"_s, 10.0, 1.0, 1000.0);
    mob.width = reader.number(u"width"_s, 0.6, 0.2, 4.0);
    mob.height = reader.number(u"height"_s, 1.8, 0.2, 4.0);
    mob.walkSpeed = reader.number(u"walkSpeed"_s, 1.5, 0.0, 20.0);
    mob.chaseSpeed = reader.number(u"chaseSpeed"_s, mob.walkSpeed * 2.0, 0.0, 20.0);

    mob.hostile = reader.boolean(u"hostile"_s, false);
    mob.attackDamage = reader.number(u"attackDamage"_s, 2.0, 0.0, 100.0);
    mob.attackRange = reader.number(u"attackRange"_s, 1.6, 0.5, 8.0);
    mob.attackCooldown = reader.number(u"attackCooldown"_s, 1.0, 0.1, 60.0);
    mob.sightRange = reader.number(u"sightRange"_s, 16.0, 1.0, 64.0);
}

// The AI (decision E-423): named by "ai", else villager with villager settings, hostile for hostile
// creatures and passive for the others. Villagers walk on their own terms: they open doors, climb
// ladders and keep to their boardwalks instead of diving off them (decision E-411), unless their
// `abilities` say otherwise.
void parseAi(QcMobDefinition &mob, QcJsonFieldReader &reader)
{
    using Ai = QcMobDefinition::Ai;
    static const QHash<QString, Ai> names = {
        {u"passive"_s, Ai::Passive}, {u"hostile"_s, Ai::Hostile}, {u"villager"_s, Ai::Villager}, {u"archer"_s, Ai::Archer},
        {u"neutral"_s, Ai::Neutral}, {u"golem"_s, Ai::Golem}};
    const Ai fallback = mob.villager ? Ai::Villager : mob.hostile ? Ai::Hostile : Ai::Passive;
    const QString name = reader.string(u"ai"_s, names.key(fallback));
    if (!names.contains(name)) {
        reader.fail(u"'ai' must be passive, hostile, villager, archer, neutral or golem"_s);
        return;
    }

    mob.ai = names.value(name);
    if ((mob.ai == Ai::Villager) != mob.villager) {
        reader.fail(u"the villager AI goes with 'villager' settings, and they with it"_s);
    } else if ((mob.ai == Ai::Hostile || mob.ai == Ai::Archer) && !mob.hostile) {
        reader.fail(u"the hostile and archer AIs need 'hostile': true"_s);
    } else if (mob.ai == Ai::Golem && (mob.hostile || mob.flying)) {
        reader.fail(u"the golem AI is for peaceful walkers"_s);
    }
    if (mob.ai == Ai::Villager) {
        mob.abilities = {true, QcMobAbilities::Climbing::Ladders, false, false};
    }
}

// How an archer shoots (decision E-426): "archer" with the projectile and the times, speed, spread
// and distances; it goes with the archer AI.
void parseArcher(QcMobDefinition &mob, QcJsonFieldReader &reader, const QString &packNamespace)
{
    QcArcherDefinition &archer = mob.archer;
    archer.projectile = reader.name(u"projectile"_s, packNamespace);
    archer.drawSeconds = reader.number(u"drawSeconds"_s, archer.drawSeconds, 0.1, 10.0);
    archer.cooldownSeconds = reader.number(u"cooldownSeconds"_s, archer.cooldownSeconds, 0.0, 60.0);
    archer.speed = reader.number(u"speed"_s, archer.speed, 1.0, 100.0);
    archer.spreadDegrees = reader.number(u"spreadDegrees"_s, archer.spreadDegrees, 0.0, 45.0);
    archer.range = reader.number(u"range"_s, archer.range, 2.0, 64.0);
    archer.minDistance = reader.number(u"minDistance"_s, archer.minDistance, 0.0, 32.0);
    archer.keepDistance = reader.number(u"keepDistance"_s, archer.keepDistance, 1.0, 64.0);
    if (archer.projectile.isEmpty()) {
        reader.fail(u"needs a 'projectile'"_s);
    } else if (archer.minDistance >= archer.keepDistance || archer.keepDistance > archer.range) {
        reader.fail(u"needs 'minDistance' < 'keepDistance' <= 'range'"_s);
    }
}

// How a golem fights, is repaired and built (decision E-471): "golem" with its push resistance,
// lift, attack sound, repair item and health, and the blocks it is built from; it goes with the
// golem AI.
void parseGolem(QcMobDefinition &mob, QcJsonFieldReader &reader, const QString &packNamespace)
{
    QcGolemDefinition &golem = mob.golem;
    golem.knockbackResistance = reader.number(u"knockbackResistance"_s, golem.knockbackResistance, 0.0, 1.0);
    golem.lift = reader.number(u"lift"_s, golem.lift, 0.0, 4.0);
    golem.attackSound = reader.name(u"attackSound"_s, packNamespace);
    golem.repairItem = reader.name(u"repairItem"_s, packNamespace);
    golem.repairHealth = reader.number(u"repairHealth"_s, golem.repairHealth, 0.0, 1000.0);
    golem.repairSound = reader.name(u"repairSound"_s, packNamespace);
    reader.nestedObject(u"build"_s, [&golem, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) {
        golem.buildBody = nested.name(u"body"_s, packNamespace);
        golem.buildHead = nested.name(u"head"_s, packNamespace);
        if (golem.buildBody.isEmpty() || golem.buildHead.isEmpty() || golem.buildBody == golem.buildHead) {
            nested.fail(u"needs a 'body' and another 'head' block"_s);
        }
    });
}

void parseAbilities(QcMobDefinition &mob, QcJsonFieldReader &reader)
{
    QcMobAbilities &abilities = mob.abilities;
    abilities.opensDoors = reader.boolean(u"opensDoors"_s, abilities.opensDoors);
    abilities.swims = reader.boolean(u"swims"_s, abilities.swims);
    abilities.divesIntoWater = reader.boolean(u"divesIntoWater"_s, abilities.divesIntoWater);

    using Climbing = QcMobAbilities::Climbing;
    static const QHash<QString, Climbing> climbing = {{u"none"_s, Climbing::None}, {u"ladders"_s, Climbing::Ladders}, {u"walls"_s, Climbing::Walls}};
    const QString climbs = reader.string(u"climbs"_s, climbing.key(abilities.climbs));
    if (!climbing.contains(climbs)) {
        reader.fail(u"'climbs' must be none, ladders or walls"_s);
    }
    abilities.climbs = climbing.value(climbs, abilities.climbs);
}

// "blowEffects": [{"effect": "poison", "seconds": 7, "level": 1}], the effects its blows give.
QList<QcStatusEffect> parseBlowEffects(const QJsonObject &json, QcJsonFieldReader &reader, const QString &packNamespace)
{
    QList<QcStatusEffect> effects;
    const QJsonValue value = json.value(u"blowEffects"_s);
    if (value.isUndefined()) {
        return effects;
    }
    const QJsonArray list = value.toArray();
    if (!value.isArray() || list.size() > 8) {
        reader.fail(u"'blowEffects' must be a list of at most 8 effects"_s);
        return effects;
    }

    for (const auto &entry : list) {
        QcJsonFieldReader nested{entry.toObject(), reader.context + u" blowEffects"_s, {}};
        QcStatusEffect effect;
        effect.name = nested.name(u"effect"_s, packNamespace);
        effect.level = nested.integer(u"level"_s, 1, 1, QcEffectDefinition::maxLevel);
        effect.ticksLeft = int(std::lround(nested.number(u"seconds"_s, 5.0, 0.05, 3600.0) * 20.0));
        if (!entry.isObject() || effect.name.isEmpty()) {
            nested.fail(u"every entry is an object with an 'effect'"_s);
        }
        reader.adopt(nested);
        effects.append(effect);
    }
    return effects;
}

// The traits of the creatures of stage 52.2 (decisions E-450, E-451, E-453): immunities, the effects
// of its blows, neutral by day, leaping, slow falling and hiding in blocks.
void parseTraits(const QJsonObject &json, QcMobDefinition &mob, QcJsonFieldReader &reader, const QString &packNamespace)
{
    mob.immuneTo = QcContentFields::parseNames(u"immuneTo"_s, packNamespace, reader);
    mob.blowEffects = parseBlowEffects(json, reader, packNamespace);
    mob.neutralByDay = reader.boolean(u"neutralByDay"_s, false);
    mob.leapSpeed = reader.number(u"leapSpeed"_s, 0.0, 0.0, 20.0);
    mob.maxFallSpeed = reader.number(u"maxFallSpeed"_s, 0.0, 0.0, 60.0);
    mob.hidesInBlocks = reader.boolean(u"hidesInBlocks"_s, false);
    if (reader.ok() && (mob.neutralByDay || !mob.blowEffects.isEmpty()) && !mob.hostile) {
        reader.fail(u"'neutralByDay' and 'blowEffects' are for hostile creatures"_s);
    }
}

// One spawn rule of a creature: the `spawn` object itself or an entry of its `also` list.
QcMobDefinition::SpawnRule parseSpawnRule(const QJsonObject &spawn, QcJsonFieldReader &reader, bool hostile, const QString &packNamespace)
{
    QcMobDefinition::SpawnRule rule;
    const QString time = reader.string(u"time"_s, u"any"_s);
    if (time == u"day"_s) {
        rule.time = QcMobDefinition::SpawnTime::Day;
    } else if (time == u"night"_s) {
        rule.time = QcMobDefinition::SpawnTime::Night;
    } else if (time != u"any"_s) {
        reader.fail(u"'time' must be day, night or any"_s);
    }

    rule.weight = reader.number(u"weight"_s, 1.0, 0.0, 100.0);
    rule.caves = reader.boolean(u"caves"_s, false);
    // Hostile creatures keep away from lit places unless their data says otherwise.
    rule.maxLight = reader.integer(u"maxLight"_s, hostile ? 7 : 15, 0, 15);
    rule.chunkChance = reader.number(u"chunkChance"_s, 1.0, 0.0, 1.0);
    rule.maxY = reader.integer(u"maxY"_s, QcWorld::worldHeight, 1, QcWorld::worldHeight);

    const QJsonArray blocks = spawn.value(u"blocks"_s).toArray();
    for (const auto &entry : blocks) {
        const QString name = QcContentNames::qualify(entry.toString(), packNamespace);
        if (name.isEmpty()) {
            reader.fail(u"invalid 'blocks' entry"_s);
            break;
        }
        rule.blocks.append(name);
    }

    const QJsonValue biomes = spawn.value(u"biomes"_s);
    if (!biomes.isUndefined() && (!biomes.isArray() || biomes.toArray().size() > 64)) {
        reader.fail(u"'biomes' must be an array of at most 64 biome names"_s);
    }
    const QJsonArray biomeList = biomes.toArray();
    for (const auto &entry : biomeList) {
        const QString name = entry.isString() ? QcContentNames::qualify(entry.toString(), packNamespace) : QString();
        if (name.isEmpty()) {
            reader.fail(u"invalid 'biomes' entry"_s);
            break;
        }
        rule.biomes.append(name);
    }
    return rule;
}

void parseSpawnConditions(const QJsonObject &spawn, QcMobDefinition &mob, QcJsonFieldReader &spawnReader, const QString &packNamespace)
{
    const QcMobDefinition::SpawnRule main = parseSpawnRule(spawn, spawnReader, mob.hostile, packNamespace);
    mob.spawnTime = main.time;
    mob.spawnBlocks = main.blocks;
    mob.spawnWeight = main.weight;
    mob.spawnsInCaves = main.caves;
    mob.maxSpawnLight = main.maxLight;
    mob.spawnChunkChance = main.chunkChance;
    mob.spawnMaxY = main.maxY;
    mob.spawnBiomes = main.biomes;
    mob.maxGroup = spawnReader.integer(u"maxGroup"_s, 2, 1, 4);
}

// Further places and times with their own rules; group size and caps stay shared.
void parseExtraSpawns(const QJsonObject &spawn, QcMobDefinition &mob, QcJsonFieldReader &spawnReader, const QString &packNamespace)
{
    const QJsonValue also = spawn.value(u"also"_s);
    if (!also.isUndefined() && (!also.isArray() || also.toArray().size() > QcMobDefinition::maxExtraSpawns)) {
        spawnReader.fail(u"'also' must be an array of at most %1 spawn rules"_s.arg(QcMobDefinition::maxExtraSpawns));
    }

    const QJsonArray alsoList = also.toArray();
    for (const auto &entry : alsoList) {
        if (!entry.isObject()) {
            spawnReader.fail(u"'also' entries must be objects"_s);
            break;
        }

        QcJsonFieldReader ruleReader{entry.toObject(), spawnReader.context + u" also"_s, {}};
        // Group size and caps belong to the creature, not to one rule.
        for (const QString &shared : {u"maxGroup"_s, u"ambient"_s, u"maxCount"_s}) {
            if (entry.toObject().contains(shared)) {
                ruleReader.fail(u"'%1' belongs to the main spawn rule"_s.arg(shared));
            }
        }

        mob.extraSpawns.append(parseSpawnRule(entry.toObject(), ruleReader, mob.hostile, packNamespace));
        if (!ruleReader.ok()) {
            spawnReader.fail(ruleReader.error);
            break;
        }
    }
}

void parseSpawnLimits(const QJsonObject &spawn, QcMobDefinition &mob, QcJsonFieldReader &spawnReader, const QString &packNamespace)
{
    mob.ambient = spawnReader.boolean(u"ambient"_s, false);
    mob.maxCount = spawnReader.integer(u"maxCount"_s, 0, 0, 64);
    parseExtraSpawns(spawn, mob, spawnReader, packNamespace);
}

void parseSpawn(const QJsonObject &json, QcMobDefinition &mob, QcJsonFieldReader &reader, const QString &packNamespace)
{
    const QJsonObject spawn = json.value(u"spawn"_s).toObject();
    QcJsonFieldReader spawnReader{spawn, reader.context + u" spawn"_s, {}};

    parseSpawnConditions(spawn, mob, spawnReader, packNamespace);
    reader.adopt(spawnReader);
    parseSpawnLimits(spawn, mob, spawnReader, packNamespace);
    reader.adopt(spawnReader);

    if (mob.ambient && mob.hostile) {
        reader.fail(u"ambient creatures cannot be hostile"_s);
    }
}

void parseCatch(QcMobDefinition &mob, QcJsonFieldReader &catchReader, const QString &packNamespace)
{
    mob.catchWith = catchReader.name(u"with"_s, packNamespace);
    mob.catchItem = catchReader.name(u"item"_s, packNamespace);
    if (catchReader.ok() && (mob.catchWith.isEmpty() || mob.catchItem.isEmpty())) {
        catchReader.fail(u"needs 'with' and 'item'"_s);
    }
}

void parseBreeding(QcMobDefinition &mob, QcJsonFieldReader &breedingReader, const QString &packNamespace)
{
    QcBreedingDefinition &breeding = mob.breeding;
    breeding.food = QcContentFields::parseNames(u"food"_s, packNamespace, breedingReader);
    breeding.loveSeconds = breedingReader.number(u"loveSeconds"_s, 30.0, 1.0, 3600.0);
    breeding.cooldownSeconds = breedingReader.number(u"cooldownSeconds"_s, 300.0, 0.0, 86400.0);
    breeding.growUpSeconds = breedingReader.number(u"growUpSeconds"_s, 1200.0, 1.0, 86400.0);
    breeding.partnerDistance = breedingReader.number(u"partnerDistance"_s, 8.0, 0.5, 32.0);
    breeding.experience = QcContentFields::parseExperience(u"experience"_s, breedingReader, breeding.experience);
    if (breedingReader.ok() && breeding.food.isEmpty()) {
        breedingReader.fail(u"needs 'food'"_s);
    }
    if (breedingReader.ok() && (mob.hostile || mob.ambient || mob.flying)) {
        breedingReader.fail(u"only peaceful animals on the ground breed"_s);
    }
}

// What a creature lays now and then (decision E-452); only creatures on the ground lay.
void parseLaying(QcMobDefinition &mob, QcJsonFieldReader &layingReader, const QString &packNamespace)
{
    QcLayingDefinition &laying = mob.laying;
    laying.item = layingReader.name(u"item"_s, packNamespace);
    laying.minSeconds = layingReader.number(u"minSeconds"_s, 300.0, 1.0, 86400.0);
    laying.maxSeconds = layingReader.number(u"maxSeconds"_s, std::max(600.0, laying.minSeconds), 1.0, 86400.0);
    laying.sound = layingReader.name(u"sound"_s, packNamespace);
    if (layingReader.ok() && laying.item.isEmpty()) {
        layingReader.fail(u"needs 'item'"_s);
    }
    if (layingReader.ok() && laying.maxSeconds < laying.minSeconds) {
        layingReader.fail(u"'maxSeconds' is less than 'minSeconds'"_s);
    }
    if (layingReader.ok() && (mob.ambient || mob.flying)) {
        layingReader.fail(u"only creatures on the ground lay"_s);
    }
}

void parseMobModel(const QJsonObject &json, QcMobDefinition &mob, QcJsonFieldReader &reader, const QString &packNamespace)
{
    mob.skeleton = reader.name(u"skeleton"_s, packNamespace);
    mob.outfit = reader.name(u"outfit"_s, packNamespace);
    mob.cape = reader.name(u"cape"_s, packNamespace);
    mob.armsForward = reader.boolean(u"armsForward"_s, false);
    if (mob.skeleton.isEmpty() != mob.outfit.isEmpty()) {
        reader.fail(u"'skeleton' and 'outfit' go together"_s);
    }
    if (!mob.cape.isEmpty() && mob.skeleton.isEmpty()) {
        reader.fail(u"'cape' needs a 'skeleton'"_s);
    }

    const QJsonArray model = json.value(u"model"_s).toArray();
    if (model.size() > 256) {
        reader.fail(u"'model' has more than 256 parts"_s);
    }

    for (const auto &entry : model) {
        const QJsonObject partJson = entry.toObject();
        const std::optional<QcVec3> from = QcContentFields::vectorField(partJson.value(u"from"_s));
        const std::optional<QcVec3> to = QcContentFields::vectorField(partJson.value(u"to"_s));
        const QString texture = QcContentNames::qualify(partJson.value(u"texture"_s).toString(), packNamespace);
        if (!from || !to || texture.isEmpty() || from->x >= to->x || from->y >= to->y || from->z >= to->z) {
            reader.fail(u"model parts need 'from' < 'to' (three numbers within 8 m) and a 'texture'"_s);
            break;
        }
        const std::optional<QcMobPart> part = partLook({*from, *to, texture}, partJson, reader);
        if (!part) {
            break;
        }
        mob.parts.append(*part);
    }

    if (mob.parts.isEmpty() && mob.skeleton.isEmpty()) {
        reader.fail(u"'model' needs at least one part (or use 'skeleton' and 'outfit')"_s);
    }
}

void parseMobSounds(const QJsonObject &json, QcMobDefinition &mob, QcJsonFieldReader &reader, const QString &packNamespace)
{
    const QJsonValue sounds = json.value(u"sounds"_s);
    if (!sounds.isUndefined() && !sounds.isObject()) {
        reader.fail(u"'sounds' must be an object"_s);
    }

    const QJsonObject soundJson = sounds.toObject();
    QcJsonFieldReader soundReader{soundJson, reader.context + u" sounds"_s, {}};
    mob.ambientSound = soundReader.name(u"ambient"_s, packNamespace);
    mob.hurtSound = soundReader.name(u"hurt"_s, packNamespace, QString::fromLatin1(QcSoundKeys::mobHurt));
    mob.deathSound = soundReader.name(u"death"_s, packNamespace, QString::fromLatin1(QcSoundKeys::mobDeath));
    reader.adopt(soundReader);
}

[[nodiscard]] QcResult<QcModelBone> parseBone(const QJsonObject &boneJson, const QString &context)
{
    QcJsonFieldReader boneReader{boneJson, context + u" bone"_s, {}};
    QcModelBone bone;
    bone.name = boneReader.string(u"name"_s);

    const QString role = boneReader.string(u"role"_s, u"none"_s);
    const std::optional<QcBoneRole> parsedRole = QcContentFields::boneRoleFromName(role);
    if (!parsedRole) {
        boneReader.fail(u"'role' must be none, head, body, right_arm, left_arm, right_leg or left_leg"_s);
    }
    bone.role = parsedRole.value_or(QcBoneRole::None);

    const std::optional<QcVec3> pivot = QcContentFields::vectorField(boneJson.value(u"pivot"_s));
    if (!pivot) {
        boneReader.fail(u"'pivot' must be three numbers within 8 m"_s);
    }
    bone.pivot = pivot.value_or(QcVec3{});

    bone.boxes = parseBoxes(boneJson.value(u"boxes"_s), QcModelDefinition::maxBoxesPerBone, boneReader);
    if (bone.name.isEmpty() || bone.name.size() > 64 || bone.boxes.isEmpty()) {
        boneReader.fail(u"bones need a 'name' (at most 64 characters) and at least one box"_s);
    }

    if (!boneReader.ok()) {
        return qcError(boneReader.error);
    }
    return bone;
}

void parseAppearanceLayers(const QJsonObject &json, QcAppearanceDefinition &appearance, QcJsonFieldReader &reader,
                           const QString &packNamespace)
{
    const QJsonValue layers = json.value(u"layers"_s);
    if (!layers.isObject() || layers.toObject().isEmpty() || layers.toObject().size() > QcAppearanceDefinition::maxLayers) {
        reader.fail(u"'layers' must map 1 to %1 slots to textures"_s.arg(QcAppearanceDefinition::maxLayers));
    }

    const QJsonObject layerMap = layers.toObject();
    for (auto it = layerMap.constBegin(); it != layerMap.constEnd() && reader.ok(); ++it) {
        const QJsonObject layerJson = it.value().toObject();
        QcJsonFieldReader layerReader{layerJson, u"%1 layer '%2'"_s.arg(reader.context, it.key()), {}};
        QcAppearanceLayer layer;
        layer.slot = it.key();
        layer.texture = layerReader.name(u"texture"_s, packNamespace);
        const std::optional<QcTint> tint = tintFromName(layerReader.string(u"tint"_s, u"none"_s));
        if (!tint) {
            layerReader.fail(u"'tint' must be none, skin, hair, shirt or pants"_s);
        }
        layer.tint = tint.value_or(QcTint::None);
        if (!it.value().isObject() || layer.texture.isEmpty() || layer.slot.size() > 64) {
            layerReader.fail(u"needs a 'texture'"_s);
        }

        reader.adopt(layerReader);
        appearance.layers.append(std::move(layer));
    }
}

[[nodiscard]] std::optional<QcAppearanceDefinition::Kind> appearanceKindFromName(const QString &name)
{
    static const QHash<QString, QcAppearanceDefinition::Kind> kinds = {
        {u"outfit"_s, QcAppearanceDefinition::Kind::Outfit},
        {u"hair"_s, QcAppearanceDefinition::Kind::Hair},
        {u"cape"_s, QcAppearanceDefinition::Kind::Cape},
    };
    const auto it = kinds.constFind(name);
    return it == kinds.constEnd() ? std::nullopt : std::optional(*it);
}

void parseCapeShape(QcCapeShape &shape, QcJsonFieldReader &reader)
{
    shape.width = reader.number(u"width"_s, shape.width, QcCapeShape::minSize, QcCapeShape::maxWidth);
    shape.length = reader.number(u"length"_s, shape.length, QcCapeShape::minSize, QcCapeShape::maxLength);
    shape.thickness = reader.number(u"thickness"_s, shape.thickness, 0.005, QcCapeShape::maxThickness);
    shape.offset = reader.number(u"offset"_s, shape.offset, 0.0, QcCapeShape::maxOffset);
}

} // namespace

namespace QcMobParser {

[[nodiscard]] QcResult<QcMobDefinition> parseMob(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"mob"_s, {}};
    QcMobDefinition mob;
    mob.name = reader.name(u"name"_s, packNamespace);
    if (mob.name.isEmpty()) {
        return qcError(reader.ok() ? u"mob: 'name' is required"_s : reader.error);
    }

    reader.context = u"mob '%1'"_s.arg(mob.name);
    mob.translationKey = reader.string(u"translation"_s, QcContentFields::translationKey(u"mob"_s, mob.name));
    mob.descriptionKey = reader.string(u"description"_s, mob.translationKey + u".description"_s);

    parseMobStats(mob, reader);
    parseSpawn(json, mob, reader, packNamespace);
    mob.flying = reader.boolean(u"flying"_s, false);
    mob.glow = reader.integer(u"glow"_s, 0, 0, QcMobDefinition::maxGlow);
    reader.nestedObject(u"catch"_s, [&mob, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) {
        parseCatch(mob, nested, packNamespace);
    });
    reader.nestedObject(u"breeding"_s, [&mob, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) {
        parseBreeding(mob, nested, packNamespace);
    });
    reader.nestedObject(u"lays"_s, [&mob, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) {
        parseLaying(mob, nested, packNamespace);
    });
    parseMobModel(json, mob, reader, packNamespace);
    // "villager" names the villager settings (decision E-421).
    if (json.contains(u"villager"_s) && !json.value(u"villager"_s).isString()) {
        reader.fail(u"'villager' names villager settings (villagers/<name>.json), e.g. \"villager\""_s);
    }
    mob.villagerSettings = reader.name(u"villager"_s, packNamespace);
    mob.villager = !mob.villagerSettings.isEmpty();
    if (reader.ok() && mob.villager && (mob.skeleton.isEmpty() || mob.hostile || mob.flying || mob.ambient)) {
        reader.fail(u"villagers need a 'skeleton' and are peaceful walkers"_s);
    }
    parseAi(mob, reader);
    reader.nestedObject(u"abilities"_s, [&mob](const QJsonObject &, QcJsonFieldReader &nested) { parseAbilities(mob, nested); });
    if (reader.ok() && mob.abilities.opensDoors && mob.ai != QcMobDefinition::Ai::Villager) {
        reader.fail(u"only the villager AI opens doors"_s);
    }
    reader.nestedObject(u"archer"_s, [&mob, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) {
        parseArcher(mob, nested, packNamespace);
    });
    if (reader.ok() && json.contains(u"archer"_s) != (mob.ai == QcMobDefinition::Ai::Archer)) {
        reader.fail(u"the archer AI goes with 'archer' settings, and they with it"_s);
    }
    reader.nestedObject(u"golem"_s, [&mob, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) {
        parseGolem(mob, nested, packNamespace);
    });
    if (reader.ok() && json.contains(u"golem"_s) != (mob.ai == QcMobDefinition::Ai::Golem)) {
        reader.fail(u"the golem AI goes with 'golem' settings, and they with it"_s);
    }
    QcMobTraitsParser::parseTraits(json, mob, reader, packNamespace);
    mob.burnsInDaylight = reader.boolean(u"burnsInDaylight"_s, false);
    parseTraits(json, mob, reader, packNamespace);
    mob.heldItem = reader.name(u"heldItem"_s, packNamespace);
    mob.drops = parseDrops(json.value(u"drops"_s), packNamespace, reader);
    mob.experience = QcContentFields::parseExperience(u"experience"_s, reader);
    parseMobSounds(json, mob, reader, packNamespace);

    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return mob;
}

[[nodiscard]] QcResult<QcModelDefinition> parseModel(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"model"_s, {}};
    QcModelDefinition model;
    model.name = reader.name(u"name"_s, packNamespace);
    if (model.name.isEmpty()) {
        return qcError(reader.ok() ? u"model: 'name' is required"_s : reader.error);
    }

    reader.context = u"model '%1'"_s.arg(model.name);
    const QJsonValue bones = json.value(u"bones"_s);
    if (!bones.isArray() || bones.toArray().isEmpty() || bones.toArray().size() > QcModelDefinition::maxBones) {
        return qcError(u"%1: 'bones' must be an array of 1 to %2 bones"_s.arg(reader.context).arg(QcModelDefinition::maxBones));
    }

    const QJsonArray boneList = bones.toArray();
    for (const auto &entry : boneList) {
        QcResult<QcModelBone> bone = parseBone(entry.toObject(), reader.context);
        if (!bone) {
            return qcError(bone.error());
        }
        model.bones.append(std::move(*bone));
    }
    return model;
}

[[nodiscard]] QcResult<QcAppearanceDefinition> parseAppearance(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"appearance"_s, {}};
    QcAppearanceDefinition appearance;
    appearance.name = reader.name(u"name"_s, packNamespace);
    if (appearance.name.isEmpty()) {
        return qcError(reader.ok() ? u"appearance: 'name' is required"_s : reader.error);
    }

    reader.context = u"appearance '%1'"_s.arg(appearance.name);
    appearance.translationKey = reader.string(u"translation"_s, QcContentFields::translationKey(u"appearance"_s, appearance.name));

    const std::optional<QcAppearanceDefinition::Kind> kind = appearanceKindFromName(reader.string(u"kind"_s, u"outfit"_s));
    if (!kind) {
        reader.fail(u"'kind' must be outfit, hair or cape"_s);
    }
    appearance.kind = kind.value_or(QcAppearanceDefinition::Kind::Outfit);
    appearance.playable = reader.boolean(u"playable"_s, true);
    if (appearance.kind == QcAppearanceDefinition::Kind::Cape) {
        reader.nestedObject(u"shape"_s, [&appearance](const QJsonObject &, QcJsonFieldReader &nested) { parseCapeShape(appearance.cape, nested); });
    }

    parseAppearanceLayers(json, appearance, reader, packNamespace);
    if (appearance.kind == QcAppearanceDefinition::Kind::Hair) {
        appearance.boxes = parseBoxes(json.value(u"boxes"_s), QcAppearanceDefinition::maxBoxes, reader);
        if (reader.ok() && appearance.boxes.isEmpty()) {
            reader.fail(u"hair styles need 'boxes'"_s);
        }
    }
    if (reader.ok() && appearance.kind == QcAppearanceDefinition::Kind::Cape && !appearance.layer(u"outside"_s)) {
        reader.fail(u"capes need an 'outside' layer"_s);
    }

    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return appearance;
}

} // namespace QcMobParser
