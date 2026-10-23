#include "common/content/parser/QcMobReader.h"

#include "common/content/definitions/QcEffectDefinition.h"
#include "common/content/definitions/QcSoundKeys.h"
#include "common/content/parser/QcContentFields.h"
#include "common/content/registry/QcContentNames.h"
#include "common/core/QcWorldConstants.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

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

// The projectiles an archer shoots instead while its target lacks an effect (decision E-623):
// "choices": [{"projectile", "unlessTargetHas"}].
[[nodiscard]] QList<QcArcherDefinition::Choice> parseArcherChoices(QcJsonFieldReader &reader, const QString &packNamespace)
{
    const QJsonValue value = reader.json.value(u"choices"_s);
    if (value.isUndefined()) {
        return {};
    }
    if (!value.isArray() || value.toArray().size() > QcArcherDefinition::maxChoices) {
        reader.fail(u"'choices' must be an array of at most %1 entries"_s.arg(QcArcherDefinition::maxChoices));
        return {};
    }

    QList<QcArcherDefinition::Choice> choices;
    const QJsonArray list = value.toArray();
    for (const auto &entry : list) {
        QcJsonFieldReader nested{entry.toObject(), reader.context + u" choices"_s, {}};
        const QcArcherDefinition::Choice choice{nested.name(u"projectile"_s, packNamespace), nested.name(u"unlessTargetHas"_s, packNamespace)};
        if (!entry.isObject() || choice.projectile.isEmpty() || choice.unlessTargetHas.isEmpty()) {
            nested.fail(u"entries need a 'projectile' and 'unlessTargetHas'"_s);
        }
        reader.adopt(nested);
        if (!reader.ok()) {
            return {};
        }
        choices.append(choice);
    }
    return choices;
}

// How an archer shoots (decision E-426): "archer" with the projectile and the times, speed, spread
// and distances; it goes with the archer AI.
[[nodiscard]] QcArcherDefinition parseArcher(QcJsonFieldReader &reader, const QString &packNamespace)
{
    QcArcherDefinition archer;
    archer.projectile = reader.name(u"projectile"_s, packNamespace);
    archer.choices = parseArcherChoices(reader, packNamespace);
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
    return archer;
}

// How a golem fights, is repaired and built (decision E-471): "golem" with its push resistance,
// lift, attack sound, repair item and health, and the blocks it is built from; it goes with the
// golem AI.
[[nodiscard]] QcGolemDefinition parseGolem(QcJsonFieldReader &reader, const QString &packNamespace)
{
    QcGolemDefinition golem;
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
    return golem;
}

// The abilities in "abilities"; missing fields keep those of `fallback` (the AI's defaults).
[[nodiscard]] QcMobAbilities parseAbilities(QcMobAbilities fallback, QcJsonFieldReader &reader)
{
    QcMobAbilities abilities = fallback;
    abilities.opensDoors = reader.boolean(u"opensDoors"_s, abilities.opensDoors);
    abilities.swims = reader.boolean(u"swims"_s, abilities.swims);
    abilities.divesIntoWater = reader.boolean(u"divesIntoWater"_s, abilities.divesIntoWater);
    abilities.dives = reader.boolean(u"dives"_s, abilities.dives);

    using Climbing = QcMobAbilities::Climbing;
    static const QHash<QString, Climbing> climbing = {{u"none"_s, Climbing::None}, {u"ladders"_s, Climbing::Ladders}, {u"walls"_s, Climbing::Walls}};
    const QString climbs = reader.string(u"climbs"_s, climbing.key(abilities.climbs));
    if (!climbing.contains(climbs)) {
        reader.fail(u"'climbs' must be none, ladders or walls"_s);
    }
    abilities.climbs = climbing.value(climbs, abilities.climbs);
    return abilities;
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
        effect.ticksLeft = static_cast<int>(std::lround(nested.number(u"seconds"_s, 5.0, 0.05, 3600.0) * 20.0));
        if (!entry.isObject() || effect.name.isEmpty()) {
            nested.fail(u"every entry is an object with an 'effect'"_s);
        }
        reader.adopt(nested);
        effects.append(effect);
    }
    return effects;
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
    // Hostile creatures need darkness unless their data says otherwise (decision E-836): no block
    // light and the original's test of the sky light.
    rule.maxLight = reader.integer(u"maxLight"_s, hostile ? 0 : 15, 0, 15);
    rule.darkness = reader.boolean(u"darkness"_s, hostile);
    rule.chunkChance = reader.number(u"chunkChance"_s, 1.0, 0.0, 1.0);
    rule.maxY = reader.integer(u"maxY"_s, QcWorld::worldHeight, 1, QcWorld::worldHeight);
    rule.sky = reader.boolean(u"sky"_s, false);
    rule.water = reader.boolean(u"water"_s, false);

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

// How a creature breeds; `mob` (read so far) must be a peaceful animal on the ground.
[[nodiscard]] QcBreedingDefinition parseBreeding(const QcMobDefinition &mob, QcJsonFieldReader &breedingReader,
                                                 const QString &packNamespace)
{
    QcBreedingDefinition breeding;
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
    return breeding;
}

// What a creature lays now and then (decision E-452); only creatures on the ground lay.
[[nodiscard]] QcLayingDefinition parseLaying(const QcMobDefinition &mob, QcJsonFieldReader &layingReader,
                                             const QString &packNamespace)
{
    QcLayingDefinition laying;
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
    return laying;
}

} // namespace

QcMobReader::QcMobReader(const QJsonObject &json, const QString &packNamespace)
    : m_json(json)
    , m_namespace(packNamespace)
    , m_reader{json, u"mob"_s, {}}
{
}

// Catching, breeding and laying: what players do with a kept creature.
void QcMobReader::parseKeeping()
{
    m_reader.nestedObject(u"catch"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) { parseCatch(nested); });
    m_reader.nestedObject(u"breeding"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        m_mob.breeding = parseBreeding(m_mob, nested, m_namespace);
    });
    m_reader.nestedObject(u"lays"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        m_mob.laying = parseLaying(m_mob, nested, m_namespace);
    });
}

// "villager" names the villager settings (decision E-421).
void QcMobReader::parseVillager()
{
    if (m_json.contains(u"villager"_s) && !m_json.value(u"villager"_s).isString()) {
        m_reader.fail(u"'villager' names villager settings (villagers/<name>.json), e.g. \"villager\""_s);
    }
    m_mob.villagerSettings = m_reader.name(u"villager"_s, m_namespace);
    m_mob.villager = !m_mob.villagerSettings.isEmpty();
    if (m_reader.ok() && m_mob.villager && (m_mob.skeleton.isEmpty() || m_mob.hostile || m_mob.flying || m_mob.ambient)) {
        m_reader.fail(u"villagers need a 'skeleton' and are peaceful walkers"_s);
    }
}

// Abilities, archers and golems; each goes with its AI.
void QcMobReader::parseFighting()
{
    m_reader.nestedObject(u"abilities"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        m_mob.abilities = parseAbilities(m_mob.abilities, nested);
    });
    if (m_reader.ok() && m_mob.abilities.opensDoors && m_mob.ai != QcMobDefinition::Ai::Villager) {
        m_reader.fail(u"only the villager AI opens doors"_s);
    }
    m_reader.nestedObject(u"archer"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        m_mob.archer = parseArcher(nested, m_namespace);
    });
    if (m_reader.ok() && m_json.contains(u"archer"_s) != (m_mob.ai == QcMobDefinition::Ai::Archer)) {
        m_reader.fail(u"the archer AI goes with 'archer' settings, and they with it"_s);
    }
    m_reader.nestedObject(u"golem"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        m_mob.golem = parseGolem(nested, m_namespace);
    });
    if (m_reader.ok() && m_json.contains(u"golem"_s) != (m_mob.ai == QcMobDefinition::Ai::Golem)) {
        m_reader.fail(u"the golem AI goes with 'golem' settings, and they with it"_s);
    }
}

QcResult<QcMobDefinition> QcMobReader::read()
{
    m_mob.name = m_reader.name(u"name"_s, m_namespace);
    if (m_mob.name.isEmpty()) {
        return qcError(m_reader.ok() ? u"mob: 'name' is required"_s : m_reader.error);
    }

    m_reader.context = u"mob '%1'"_s.arg(m_mob.name);
    m_mob.translationKey = m_reader.string(u"translation"_s, QcContentFields::translationKey(u"mob"_s, m_mob.name));
    m_mob.descriptionKey = m_reader.string(u"description"_s, m_mob.translationKey + u".description"_s);

    parseMobStats();
    parseSpawn();
    m_mob.flying = m_reader.boolean(u"flying"_s, false);
    m_mob.glow = m_reader.integer(u"glow"_s, 0, 0, QcMobDefinition::maxGlow);
    parseKeeping();
    parseMobModel();
    parseVillager();
    parseAi();
    parseFighting();
    parseCreatureTraits();
    m_mob.burnsInDaylight = m_reader.boolean(u"burnsInDaylight"_s, false);
    parseHostileTraits();
    m_mob.heldItem = m_reader.name(u"heldItem"_s, m_namespace);
    m_mob.drops = parseDrops(m_json.value(u"drops"_s), m_namespace, m_reader);
    m_mob.experience = QcContentFields::parseExperience(u"experience"_s, m_reader);
    parseMobSounds();
    parseFamily();
    checkWaterSpawns();

    if (!m_reader.ok()) {
        return qcError(m_reader.error);
    }
    return std::move(m_mob);
}

void QcMobReader::parseMobStats()
{
    m_mob.maxHealth = m_reader.number(u"health"_s, 10.0, 1.0, 1000.0);
    m_mob.width = m_reader.number(u"width"_s, 0.6, 0.2, 4.0);
    m_mob.height = m_reader.number(u"height"_s, 1.8, 0.2, 4.0);
    m_mob.walkSpeed = m_reader.number(u"walkSpeed"_s, 1.5, 0.0, 20.0);
    m_mob.chaseSpeed = m_reader.number(u"chaseSpeed"_s, m_mob.walkSpeed * 2.0, 0.0, 20.0);

    m_mob.hostile = m_reader.boolean(u"hostile"_s, false);
    m_mob.attackDamage = m_reader.number(u"attackDamage"_s, 2.0, 0.0, 100.0);
    m_mob.attackRange = m_reader.number(u"attackRange"_s, 1.6, 0.5, 8.0);
    m_mob.attackCooldown = m_reader.number(u"attackCooldown"_s, 1.0, 0.1, 60.0);
    m_mob.sightRange = m_reader.number(u"sightRange"_s, 16.0, 1.0, 64.0);
}

// The AI (decision E-423): named by "ai", else villager with villager settings, hostile for hostile
// creatures and passive for the others. Villagers walk on their own terms: they open doors, climb
// ladders and keep to their boardwalks instead of diving off them (decision E-411), unless their
// `abilities` say otherwise.
void QcMobReader::parseAi()
{
    using Ai = QcMobDefinition::Ai;
    static const QHash<QString, Ai> names = {
        {u"passive"_s, Ai::Passive}, {u"hostile"_s, Ai::Hostile}, {u"villager"_s, Ai::Villager}, {u"archer"_s, Ai::Archer},
        {u"neutral"_s, Ai::Neutral}, {u"golem"_s, Ai::Golem}, {u"guardian"_s, Ai::Guardian}};
    const Ai fallback = m_mob.villager ? Ai::Villager : m_mob.hostile ? Ai::Hostile : Ai::Passive;
    const QString name = m_reader.string(u"ai"_s, names.key(fallback));
    if (!names.contains(name)) {
        m_reader.fail(u"'ai' must be passive, hostile, villager, archer, neutral, golem or guardian"_s);
        return;
    }

    m_mob.ai = names.value(name);
    if ((m_mob.ai == Ai::Villager) != m_mob.villager) {
        m_reader.fail(u"the villager AI goes with 'villager' settings, and they with it"_s);
    } else if ((m_mob.ai == Ai::Hostile || m_mob.ai == Ai::Archer || m_mob.ai == Ai::Guardian) && !m_mob.hostile) {
        m_reader.fail(u"the hostile, archer and guardian AIs need 'hostile': true"_s);
    } else if (m_mob.ai == Ai::Golem && (m_mob.hostile || m_mob.flying)) {
        m_reader.fail(u"the golem AI is for peaceful walkers"_s);
    }
    if (m_mob.ai == Ai::Villager) {
        m_mob.abilities = {true, QcMobAbilities::Climbing::Ladders, false, false};
    }
}

// The traits of the hostile creatures of stage 52.2 (decisions E-450, E-451, E-453): immunities, the effects
// of its blows, neutral by day, leaping, slow falling and hiding in blocks.
void QcMobReader::parseHostileTraits()
{
    m_mob.immuneTo = QcContentFields::parseNames(u"immuneTo"_s, m_namespace, m_reader);
    m_mob.blowEffects = parseBlowEffects(m_json, m_reader, m_namespace);
    m_mob.neutralByDay = m_reader.boolean(u"neutralByDay"_s, false);
    m_mob.leapSpeed = m_reader.number(u"leapSpeed"_s, 0.0, 0.0, 20.0);
    m_mob.maxFallSpeed = m_reader.number(u"maxFallSpeed"_s, 0.0, 0.0, 60.0);
    m_mob.hidesInBlocks = m_reader.boolean(u"hidesInBlocks"_s, false);
    if (m_reader.ok() && (m_mob.neutralByDay || !m_mob.blowEffects.isEmpty()) && !m_mob.hostile) {
        m_reader.fail(u"'neutralByDay' and 'blowEffects' are for hostile creatures"_s);
    }
}

void QcMobReader::parseSpawnConditions(const QJsonObject &spawn, QcJsonFieldReader &spawnReader)
{
    const QcMobDefinition::SpawnRule main = parseSpawnRule(spawn, spawnReader, m_mob.hostile, m_namespace);
    m_mob.spawnTime = main.time;
    m_mob.spawnBlocks = main.blocks;
    m_mob.spawnWeight = main.weight;
    m_mob.spawnsInCaves = main.caves;
    m_mob.maxSpawnLight = main.maxLight;
    m_mob.spawnInDarkness = main.darkness;
    m_mob.spawnChunkChance = main.chunkChance;
    m_mob.spawnMaxY = main.maxY;
    m_mob.spawnBiomes = main.biomes;
    m_mob.spawnUnderSky = main.sky;
    m_mob.spawnInWater = main.water;
    m_mob.maxGroup = spawnReader.integer(u"maxGroup"_s, 2, 1, 4);
}

// Further places and times with their own rules; group size and caps stay shared.
void QcMobReader::parseExtraSpawns(const QJsonObject &spawn, QcJsonFieldReader &spawnReader)
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

        m_mob.extraSpawns.append(parseSpawnRule(entry.toObject(), ruleReader, m_mob.hostile, m_namespace));
        if (!ruleReader.ok()) {
            spawnReader.fail(ruleReader.error);
            break;
        }
    }
}

void QcMobReader::parseSpawnLimits(const QJsonObject &spawn, QcJsonFieldReader &spawnReader)
{
    m_mob.ambient = spawnReader.boolean(u"ambient"_s, false);
    m_mob.maxCount = spawnReader.integer(u"maxCount"_s, 0, 0, 64);
    parseExtraSpawns(spawn, spawnReader);
}

void QcMobReader::parseSpawn()
{
    const QJsonObject spawn = m_json.value(u"spawn"_s).toObject();
    QcJsonFieldReader spawnReader{spawn, m_reader.context + u" spawn"_s, {}};

    parseSpawnConditions(spawn, spawnReader);
    m_reader.adopt(spawnReader);
    parseSpawnLimits(spawn, spawnReader);
    m_reader.adopt(spawnReader);

    if (m_mob.ambient && m_mob.hostile) {
        m_reader.fail(u"ambient creatures cannot be hostile"_s);
    }
}

void QcMobReader::parseCatch(QcJsonFieldReader &catchReader)
{
    m_mob.catchWith = catchReader.name(u"with"_s, m_namespace);
    m_mob.catchItem = catchReader.name(u"item"_s, m_namespace);
    m_mob.catchSound = catchReader.name(u"sound"_s, m_namespace, QString::fromLatin1(QcSoundKeys::bottleCatch));
    if (catchReader.ok() && (m_mob.catchWith.isEmpty() || m_mob.catchItem.isEmpty())) {
        catchReader.fail(u"needs 'with' and 'item'"_s);
    }
}

void QcMobReader::parseMobModel()
{
    m_mob.skeleton = m_reader.name(u"skeleton"_s, m_namespace);
    m_mob.outfit = m_reader.name(u"outfit"_s, m_namespace);
    m_mob.cape = m_reader.name(u"cape"_s, m_namespace);
    m_mob.armsForward = m_reader.boolean(u"armsForward"_s, false);
    if (m_mob.skeleton.isEmpty() != m_mob.outfit.isEmpty()) {
        m_reader.fail(u"'skeleton' and 'outfit' go together"_s);
    }
    if (!m_mob.cape.isEmpty() && m_mob.skeleton.isEmpty()) {
        m_reader.fail(u"'cape' needs a 'skeleton'"_s);
    }

    const QJsonArray model = m_json.value(u"model"_s).toArray();
    if (model.size() > 256) {
        m_reader.fail(u"'model' has more than 256 parts"_s);
    }

    for (const auto &entry : model) {
        const QJsonObject partJson = entry.toObject();
        const std::optional<QcVec3> from = QcContentFields::vectorField(partJson.value(u"from"_s));
        const std::optional<QcVec3> to = QcContentFields::vectorField(partJson.value(u"to"_s));
        const QString texture = QcContentNames::qualify(partJson.value(u"texture"_s).toString(), m_namespace);
        if (!from || !to || texture.isEmpty() || from->x >= to->x || from->y >= to->y || from->z >= to->z) {
            m_reader.fail(u"model parts need 'from' < 'to' (three numbers within 8 m) and a 'texture'"_s);
            break;
        }
        const std::optional<QcMobPart> part = partLook({*from, *to, texture}, partJson, m_reader);
        if (!part) {
            break;
        }
        m_mob.parts.append(*part);
    }

    if (m_mob.parts.isEmpty() && m_mob.skeleton.isEmpty()) {
        m_reader.fail(u"'model' needs at least one part (or use 'skeleton' and 'outfit')"_s);
    }
}

void QcMobReader::parseMobSounds()
{
    const QJsonValue sounds = m_json.value(u"sounds"_s);
    if (!sounds.isUndefined() && !sounds.isObject()) {
        m_reader.fail(u"'sounds' must be an object"_s);
    }

    const QJsonObject soundJson = sounds.toObject();
    QcJsonFieldReader soundReader{soundJson, m_reader.context + u" sounds"_s, {}};
    m_mob.ambientSound = soundReader.name(u"ambient"_s, m_namespace);
    m_mob.hurtSound = soundReader.name(u"hurt"_s, m_namespace, QString::fromLatin1(QcSoundKeys::mobHurt));
    m_mob.deathSound = soundReader.name(u"death"_s, m_namespace, QString::fromLatin1(QcSoundKeys::mobDeath));
    m_reader.adopt(soundReader);
}

// A parent that splits off children (decision E-839): "family": {"child", "maxChildren",
// "childrenPerHit", "maxSplits", "popDrops", "popSound", "orbitRadius", "orbitSeconds",
// "tetherTexture", "tetherRange"}, and hovering: "hover": {"height", "bob", "bobSeconds",
// "maxHeight", "turnSpeed"}.
void QcMobReader::parseFamily()
{
    m_reader.nestedObject(u"hover"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        QcHoverDefinition &hover = m_mob.hover.emplace();
        hover.height = nested.number(u"height"_s, hover.height, 0.0, 2.0);
        hover.bob = nested.number(u"bob"_s, hover.bob, 0.0, 0.5);
        hover.bobSeconds = nested.number(u"bobSeconds"_s, hover.bobSeconds, 0.1, 30.0);
        hover.maxHeight = nested.number(u"maxHeight"_s, hover.maxHeight, hover.height, 16.0);
        hover.turnSpeed = nested.number(u"turnSpeed"_s, hover.turnSpeed, 1.0, 3600.0);
    });
    m_reader.nestedObject(u"family"_s, [this](const QJsonObject &json, QcJsonFieldReader &nested) {
        QcFamilyDefinition &family = m_mob.family.emplace();
        family.child = nested.name(u"child"_s, m_namespace);
        family.maxChildren = nested.integer(u"maxChildren"_s, family.maxChildren, 1, QcFamilyDefinition::maxChildrenLimit);
        family.childrenPerHit = nested.integer(u"childrenPerHit"_s, family.childrenPerHit, 1, QcFamilyDefinition::maxChildrenLimit);
        // 0 (the default): twice as many as live at once.
        const int splits = nested.integer(u"maxSplits"_s, 0, 0, 16 * QcFamilyDefinition::maxChildrenLimit);
        family.maxSplits = splits > 0 ? splits : 2 * family.maxChildren;
        family.popDrops = parseDrops(json.value(u"popDrops"_s), m_namespace, nested);
        family.popSound = nested.name(u"popSound"_s, m_namespace);
        family.orbitRadius = nested.number(u"orbitRadius"_s, family.orbitRadius, 0.3, 8.0);
        family.orbitSeconds = nested.number(u"orbitSeconds"_s, family.orbitSeconds, 0.5, 60.0);
        family.tetherTexture = QcContentNames::qualify(json.value(u"tetherTexture"_s).toString(), m_namespace);
        family.tetherRange = nested.number(u"tetherRange"_s, family.tetherRange, 1.0, 64.0);
        if (nested.ok() && (family.child.isEmpty() || family.child == m_mob.name)) {
            nested.fail(u"needs a 'child' of another kind"_s);
        }
    });
    if (m_reader.ok() && (m_mob.family || m_mob.hover) && (m_mob.flying || m_mob.aquatic)) {
        m_reader.fail(u"'family' and 'hover' go with creatures that live on land"_s);
    }
}

// Spawning in water (drowned, decision E-838) is for creatures that walk or swim on paths, not for
// flying ones, hoverers or water creatures (those spawn by `blocks` and `aquatic`).
void QcMobReader::checkWaterSpawns()
{
    const QList<QcMobDefinition::SpawnRule> rules = m_mob.spawnRules();
    const bool water = std::ranges::any_of(rules, &QcMobDefinition::SpawnRule::water);
    if (m_reader.ok() && water && (m_mob.flying || m_mob.aquatic || m_mob.hover)) {
        m_reader.fail(u"'water' spawn rules are for creatures that walk; water creatures use 'aquatic'"_s);
    }
}
