#include "QcMobParser.h"

#include "QcContentFields.h"
#include "QcContentNames.h"
#include "QcJsonFieldReader.h"
#include "QcSoundKeys.h"
#include "QcWorldConstants.h"

#include <QJsonArray>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

std::optional<QcVec3> vectorField(const QJsonValue &value)
{
    const QJsonArray array = value.toArray();
    if (!value.isArray() || array.size() != 3 || !array.at(0).isDouble() || !array.at(1).isDouble() || !array.at(2).isDouble()) {
        return std::nullopt;
    }

    const QcVec3 vector{array.at(0).toDouble(), array.at(1).toDouble(), array.at(2).toDouble()};
    constexpr double limit = 8.0;
    if (std::abs(vector.x) > limit || std::abs(vector.y) > limit || std::abs(vector.z) > limit) {
        return std::nullopt;
    }
    return vector;
}

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
        const std::optional<QcVec3> from = vectorField(boxJson.value(u"from"_s));
        const std::optional<QcVec3> to = vectorField(boxJson.value(u"to"_s));
        const QString slot = boxJson.value(u"slot"_s).toString();
        if (!from || !to || slot.isEmpty() || slot.size() > 64 || from->x >= to->x || from->y >= to->y || from->z >= to->z) {
            reader.fail(u"boxes need 'from' < 'to' (three numbers within 8 m) and a 'slot'"_s);
            return {};
        }
        boxes.append({*from, *to, slot});
    }
    return boxes;
}

std::optional<QcBoneRole> boneRoleFromName(const QString &name)
{
    static const QHash<QString, QcBoneRole> roles = {
        {u"none"_s, QcBoneRole::None},          {u"head"_s, QcBoneRole::Head},
        {u"body"_s, QcBoneRole::Body},          {u"right_arm"_s, QcBoneRole::RightArm},
        {u"left_arm"_s, QcBoneRole::LeftArm},   {u"right_leg"_s, QcBoneRole::RightLeg},
        {u"left_leg"_s, QcBoneRole::LeftLeg},
    };
    const auto it = roles.constFind(name);
    return it == roles.constEnd() ? std::nullopt : std::optional<QcBoneRole>(*it);
}

std::optional<QcTint> tintFromName(const QString &name)
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
        const QcItemDrop drop{dropReader.name(u"item"_s, packNamespace), dropReader.integer(u"count"_s, 1, 1, 64)};
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

void parseMobModel(const QJsonObject &json, QcMobDefinition &mob, QcJsonFieldReader &reader, const QString &packNamespace)
{
    mob.skeleton = reader.name(u"skeleton"_s, packNamespace);
    mob.outfit = reader.name(u"outfit"_s, packNamespace);
    mob.armsForward = reader.boolean(u"armsForward"_s, false);
    if (mob.skeleton.isEmpty() != mob.outfit.isEmpty()) {
        reader.fail(u"'skeleton' and 'outfit' go together"_s);
    }

    const QJsonArray model = json.value(u"model"_s).toArray();
    if (model.size() > 256) {
        reader.fail(u"'model' has more than 256 parts"_s);
    }

    for (const auto &entry : model) {
        const QJsonObject partJson = entry.toObject();
        const std::optional<QcVec3> from = vectorField(partJson.value(u"from"_s));
        const std::optional<QcVec3> to = vectorField(partJson.value(u"to"_s));
        const QString texture = QcContentNames::qualify(partJson.value(u"texture"_s).toString(), packNamespace);
        if (!from || !to || texture.isEmpty() || from->x >= to->x || from->y >= to->y || from->z >= to->z) {
            reader.fail(u"model parts need 'from' < 'to' (three numbers within 8 m) and a 'texture'"_s);
            break;
        }
        mob.parts.append({*from, *to, texture});
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

QcResult<QcModelBone> parseBone(const QJsonObject &boneJson, const QString &context)
{
    QcJsonFieldReader boneReader{boneJson, context + u" bone"_s, {}};
    QcModelBone bone;
    bone.name = boneReader.string(u"name"_s);

    const QString role = boneReader.string(u"role"_s, u"none"_s);
    const std::optional<QcBoneRole> parsedRole = boneRoleFromName(role);
    if (!parsedRole) {
        boneReader.fail(u"'role' must be none, head, body, right_arm, left_arm, right_leg or left_leg"_s);
    }
    bone.role = parsedRole.value_or(QcBoneRole::None);

    const std::optional<QcVec3> pivot = vectorField(boneJson.value(u"pivot"_s));
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

} // namespace

namespace QcMobParser {

QcResult<QcMobDefinition> parseMob(const QJsonObject &json, const QString &packNamespace)
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
    parseMobModel(json, mob, reader, packNamespace);
    mob.drops = parseDrops(json.value(u"drops"_s), packNamespace, reader);
    parseMobSounds(json, mob, reader, packNamespace);

    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return mob;
}

QcResult<QcModelDefinition> parseModel(const QJsonObject &json, const QString &packNamespace)
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

QcResult<QcAppearanceDefinition> parseAppearance(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"appearance"_s, {}};
    QcAppearanceDefinition appearance;
    appearance.name = reader.name(u"name"_s, packNamespace);
    if (appearance.name.isEmpty()) {
        return qcError(reader.ok() ? u"appearance: 'name' is required"_s : reader.error);
    }

    reader.context = u"appearance '%1'"_s.arg(appearance.name);
    appearance.translationKey = reader.string(u"translation"_s, QcContentFields::translationKey(u"appearance"_s, appearance.name));

    const QString kind = reader.string(u"kind"_s, u"outfit"_s);
    if (kind == u"hair"_s) {
        appearance.kind = QcAppearanceDefinition::Kind::Hair;
    } else if (kind != u"outfit"_s) {
        reader.fail(u"'kind' must be outfit or hair"_s);
    }
    appearance.playable = reader.boolean(u"playable"_s, true);

    parseAppearanceLayers(json, appearance, reader, packNamespace);
    if (appearance.kind == QcAppearanceDefinition::Kind::Hair) {
        appearance.boxes = parseBoxes(json.value(u"boxes"_s), QcAppearanceDefinition::maxBoxes, reader);
        if (reader.ok() && appearance.boxes.isEmpty()) {
            reader.fail(u"hair styles need 'boxes'"_s);
        }
    }

    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return appearance;
}

} // namespace QcMobParser
