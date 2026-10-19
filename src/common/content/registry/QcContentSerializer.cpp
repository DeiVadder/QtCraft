#include "common/content/registry/QcContentSerializer.h"

#include "common/content/registry/QcArmourSerializer.h"
#include "common/content/registry/QcBlockSerializer.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/protocol/QcStreamLimits.h"

#include <QJsonDocument>

#include <algorithm>
#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

template<typename Definition, typename Write>
void writeList(QDataStream &stream, const QList<Definition> &list, Write write)
{
    stream << quint32(list.size());
    for (const Definition &definition : list) {
        write(stream, definition);
    }
}

// Model coordinates feed the mesher: anything beyond what the parser allows is corrupt.
bool plausibleVector(const QcVec3 &v)
{
    constexpr double limit = 8.0;
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::abs(v.x) <= limit && std::abs(v.y) <= limit
           && std::abs(v.z) <= limit;
}

void writeBoxes(QDataStream &stream, const QList<QcModelBox> &boxes)
{
    stream << quint32(boxes.size());
    for (const QcModelBox &box : boxes) {
        stream << box.from.x << box.from.y << box.from.z << box.to.x << box.to.y << box.to.z << box.slot;
    }
}

[[nodiscard]] std::optional<QList<QcModelBox>> readBoxes(QDataStream &stream, quint32 limit)
{
    const std::optional<quint32> countRead = QcStreamLimits::readCount(stream, limit);
    if (!countRead) {
        return std::nullopt;
    }
    const quint32 count = *countRead;

    QList<QcModelBox> boxes;
    for (quint32 i = 0; i < count; ++i) {
        QcModelBox box;
        stream >> box.from.x >> box.from.y >> box.from.z >> box.to.x >> box.to.y >> box.to.z >> box.slot;
        if (stream.status() != QDataStream::Ok || !plausibleVector(box.from) || !plausibleVector(box.to)) {
            return std::nullopt;
        }
        boxes.append(std::move(box));
    }
    return boxes;
}

void writeItem(QDataStream &stream, const QcItemDefinition &item)
{
    stream << item.name << item.translationKey << item.texture << qint32(item.maxStack) << item.placesBlock << item.tool
           << qint32(item.toolLevel) << item.toolSpeed << qint32(item.durability) << item.attackDamage << qint32(item.food)
           << item.fuelSeconds << quint32(item.tags.size());
    for (const QString &tag : item.tags) {
        stream << tag;
    }
    stream << item.scoops << item.emptiesTo << item.bottles << item.placesStand << item.cartBlock << item.ignites;
    QcArmourSerializer::write(stream, item.armour);
    // The launcher (decision E-425): the client draws it while the use button is held, only with
    // ammunition (the projectile kind, checked when the content was finished) outside creative mode.
    stream << item.launcher.has_value();
    if (item.launcher) {
        stream << item.launcher->drawSeconds << item.launcher->speed << item.launcher->minPower << item.launcher->ammunition;
    }
    stream << item.projectile;
    // Thrown by hand (decision E-427): the client swings the arm and keeps the cooldown.
    stream << item.throwable.has_value();
    if (item.throwable) {
        stream << item.throwable->speed << item.throwable->cooldownSeconds << item.throwable->inaccuracyDegrees;
    }
    // Drunk with the use button (decision E-500): the client sends ConsumeItem.
    stream << item.drink.has_value();
    if (item.drink) {
        stream << item.drink->clearsEffects;
    }
    // A fishing rod (decision E-503): the client sends its use as ThrowItem; the rest is the server's.
    stream << item.fishing.has_value();
}

void writeMob(QDataStream &stream, const QcMobDefinition &mob)
{
    stream << mob.name << mob.translationKey << mob.maxHealth << mob.width << mob.height << mob.hostile
           << quint32(mob.parts.size());
    for (const QcMobPart &part : mob.parts) {
        stream << part.from.x << part.from.y << part.from.z << part.to.x << part.to.y << part.to.z << part.texture << quint8(part.tint)
               << part.sitOffset.x << part.sitOffset.y << part.sitOffset.z;
    }
    stream << mob.skeleton << mob.outfit << mob.cape << mob.armsForward << mob.villager << mob.ambientSound << mob.hurtSound << mob.deathSound
           << mob.flying << qint32(mob.glow) << mob.catchWith << mob.catchItem << quint32(mob.breeding.food.size());
    for (const QString &food : mob.breeding.food) {
        stream << food;
    }
    // Every use of a tameable creature goes to the server (decision E-480).
    stream << mob.tameable.has_value();
    // Its milking item is used on it (decision E-500).
    stream << mob.milkWith;
}

void writeRecipe(QDataStream &stream, const QcRecipeDefinition &recipe)
{
    stream << recipe.name << recipe.shaped << quint32(recipe.pattern.size());
    for (const QString &row : recipe.pattern) {
        stream << row;
    }

    stream << quint32(recipe.keys.size());
    for (const auto &[key, item] : recipe.keys) {
        stream << key << item;
    }

    stream << quint32(recipe.ingredients.size());
    for (const QString &ingredient : recipe.ingredients) {
        stream << ingredient;
    }

    stream << recipe.resultItem << qint32(recipe.resultCount);
}

// Texts per language, then per key.
using Translations = QHash<QString, QHash<QString, QString>>;

void writeTranslations(QDataStream &stream, const Translations &translations)
{
    stream << quint32(translations.size());
    for (auto language = translations.constBegin(); language != translations.constEnd(); ++language) {
        stream << language.key() << quint32(language->size());
        for (auto entry = language->constBegin(); entry != language->constEnd(); ++entry) {
            stream << entry.key() << entry.value();
        }
    }
}

void writeModel(QDataStream &stream, const QcModelDefinition &model)
{
    stream << model.name << quint32(model.bones.size());
    for (const QcModelBone &bone : model.bones) {
        stream << bone.name << quint8(bone.role) << bone.pivot.x << bone.pivot.y << bone.pivot.z;
        writeBoxes(stream, bone.boxes);
    }
}

void writeAppearance(QDataStream &stream, const QcAppearanceDefinition &appearance)
{
    stream << appearance.name << appearance.translationKey << quint8(appearance.kind) << appearance.playable
           << quint32(appearance.layers.size());
    for (const QcAppearanceLayer &layer : appearance.layers) {
        stream << layer.slot << layer.texture << quint8(layer.tint);
    }
    writeBoxes(stream, appearance.boxes);
    stream << appearance.cape.width << appearance.cape.length << appearance.cape.thickness << appearance.cape.offset;
}

void writeSound(QDataStream &stream, const QcSoundDefinition &sound)
{
    stream << sound.name << quint8(sound.bus) << sound.volume << sound.pitchVariation << qint32(sound.variants) << sound.loop
           << sound.range << quint32(sound.layers.size());
    for (const QcSoundLayer &layer : sound.layers) {
        stream << quint8(layer.wave) << layer.frequency << layer.endFrequency << layer.start << layer.duration << layer.attack
               << layer.curve << layer.gain << layer.lowpass << layer.highpass << layer.tremoloRate << layer.tremoloDepth
               << layer.density;
    }
}

// Reads a count-prefixed table; `readEntry()` reads and registers one entry and returns false on
// corrupt data.
template<typename ReadEntry>
[[nodiscard]] QcStatus readTable(QDataStream &stream, quint32 limit, const QString &tableError, const QString &entryError, ReadEntry readEntry)
{
    const std::optional<quint32> countRead = QcStreamLimits::readCount(stream, limit);
    if (!countRead) {
        return qcError(tableError);
    }
    const quint32 count = *countRead;

    for (quint32 i = 0; i < count; ++i) {
        if (!readEntry()) {
            return qcError(entryError.arg(i));
        }
    }
    return {};
}

bool readItem(QDataStream &stream, QcContentRegistry &registry)
{
    QcItemDefinition item;
    qint32 maxStack = 0;
    qint32 toolLevel = 0;
    qint32 durability = 0;
    qint32 food = 0;
    stream >> item.name >> item.translationKey >> item.texture >> maxStack >> item.placesBlock >> item.tool >> toolLevel
        >> item.toolSpeed >> durability >> item.attackDamage >> food >> item.fuelSeconds;
    const std::optional<quint32> tagCountRead = QcStreamLimits::readCount(stream, 16);
    if (!tagCountRead) {
        return false;
    }
    const quint32 tagCount = *tagCountRead;

    for (quint32 t = 0; t < tagCount; ++t) {
        QString tag;
        stream >> tag;
        item.tags.append(tag);
    }

    stream >> item.scoops >> item.emptiesTo >> item.bottles >> item.placesStand >> item.cartBlock >> item.ignites;
    std::optional<std::optional<QcArmourDefinition>> armour = QcArmourSerializer::read(stream);
    if (!armour) {
        return false;
    }
    item.armour = std::move(*armour);
    bool launcher = false;
    stream >> launcher;
    if (launcher) {
        QcLauncherDefinition read;
        stream >> read.drawSeconds >> read.speed >> read.minPower >> read.ammunition;
        if (!(read.drawSeconds >= 0.05 && read.drawSeconds <= 10.0) || !(read.minPower >= 0.0 && read.minPower <= 1.0)
            || !(read.speed >= 1.0 && read.speed <= QcProjectileDefinition::maxSpeed) || read.ammunition.size() > 64) {
            return false;
        }
        item.launcher = read;
    }
    stream >> item.projectile;
    bool throwable = false;
    stream >> throwable;
    if (throwable) {
        QcThrowableDefinition read;
        stream >> read.speed >> read.cooldownSeconds >> read.inaccuracyDegrees;
        if (!(read.cooldownSeconds >= 0.0 && read.cooldownSeconds <= 60.0) || !(read.speed >= 1.0 && read.speed <= QcProjectileDefinition::maxSpeed)
            || !(read.inaccuracyDegrees >= 0.0 && read.inaccuracyDegrees <= 45.0)) {
            return false;
        }
        item.throwable = read;
    }
    bool drink = false;
    stream >> drink;
    if (drink) {
        stream >> item.drink.emplace().clearsEffects;
    }
    bool fishing = false;
    stream >> fishing;
    if (fishing) {
        item.fishing.emplace();
    }
    item.maxStack = maxStack;
    item.toolLevel = toolLevel;
    item.durability = durability;
    item.food = food;

    return stream.status() == QDataStream::Ok && food >= 0 && food <= QcItemDefinition::maxFood
           && QcBlockSerializer::plausibleFuel(item.fuelSeconds) && registry.registerItem(std::move(item));
}

[[nodiscard]] QcStatus readItems(QDataStream &stream, QcContentRegistry &registry)
{
    qsizetype itemCount = 0;
    const QcStatus items = readTable(stream, QcStreamLimits::maxDefinitions, u"Corrupt content: item table"_s,
                                     u"Corrupt content: item %1"_s, [&] {
                                         ++itemCount;
                                         return readItem(stream, registry);
                                     });

    if (!items) {
        return items;
    }

    if (registry.itemCount() != itemCount) {
        return qcError(u"Corrupt content: duplicate item names"_s);
    }
    return {};
}

[[nodiscard]] std::optional<QcMobDefinition> readMob(QDataStream &stream)
{
    QcMobDefinition mob;

    stream >> mob.name >> mob.translationKey >> mob.maxHealth >> mob.width >> mob.height >> mob.hostile;
    const std::optional<quint32> partCountRead = QcStreamLimits::readCount(stream, 256);
    if (!partCountRead) {
        return std::nullopt;
    }
    const quint32 partCount = *partCountRead;

    for (quint32 p = 0; p < partCount; ++p) {
        QcMobPart part;
        quint8 tint = 0;
        stream >> part.from.x >> part.from.y >> part.from.z >> part.to.x >> part.to.y >> part.to.z >> part.texture >> tint >> part.sitOffset.x
            >> part.sitOffset.y >> part.sitOffset.z;
        if (tint != quint8(QcTint::None) && tint != quint8(QcTint::Collar)) {
            return std::nullopt;
        }
        part.tint = QcTint(tint);
        mob.parts.append(part);
    }

    qint32 glow = 0;
    stream >> mob.skeleton >> mob.outfit >> mob.cape >> mob.armsForward >> mob.villager >> mob.ambientSound >> mob.hurtSound >> mob.deathSound
        >> mob.flying >> glow >> mob.catchWith >> mob.catchItem;
    mob.glow = std::clamp(int(glow), 0, QcMobDefinition::maxGlow);
    // What it eats for breeding, so the client knows to use it on the creature (decision E-293).
    const std::optional<quint32> foodCountRead = QcStreamLimits::readCount(stream, 64);
    if (!foodCountRead) {
        return std::nullopt;
    }
    const quint32 foodCount = *foodCountRead;
    for (quint32 f = 0; f < foodCount; ++f) {
        QString food;
        stream >> food;
        mob.breeding.food.append(food);
    }
    bool tameable = false;
    stream >> tameable;
    if (tameable) {
        mob.tameable.emplace();
    }
    stream >> mob.milkWith;

    // Sizes feed physics and the mesher: reject anything outside what the parser allows.
    const auto plausible = [](double value) { return std::isfinite(value) && std::abs(value) <= 64.0; };
    bool valid = std::isfinite(mob.maxHealth) && mob.maxHealth > 0.0 && mob.maxHealth <= 1000.0 && plausible(mob.width) && plausible(mob.height) && mob.width > 0.0
                 && mob.height > 0.0;
    for (const QcMobPart &part : std::as_const(mob.parts)) {
        valid = valid && plausible(part.from.x) && plausible(part.from.y) && plausible(part.from.z)
                && plausible(part.to.x) && plausible(part.to.y) && plausible(part.to.z) && part.sitOffset.length() <= 1.0;
    }
    if (stream.status() != QDataStream::Ok || !valid) {
        return std::nullopt;
    }
    return mob;
}

// Bounded reads: QDataStream's own list operator would reserve whatever count arrives.
[[nodiscard]] std::optional<QStringList> readRecipeStrings(QDataStream &stream)
{
    const std::optional<quint32> countRead = QcStreamLimits::readCount(stream, 9);
    if (!countRead) {
        return std::nullopt;
    }
    const quint32 count = *countRead;

    QStringList list;
    for (quint32 n = 0; n < count; ++n) {
        QString text;
        stream >> text;
        list.append(text);
    }
    return list;
}

[[nodiscard]] std::optional<QcRecipeDefinition> readRecipe(QDataStream &stream)
{
    QcRecipeDefinition recipe;

    qint32 resultCount = 0;
    stream >> recipe.name >> recipe.shaped;
    std::optional<QStringList> pattern = readRecipeStrings(stream);
    if (!pattern) {
        return std::nullopt;
    }
    recipe.pattern = std::move(*pattern);
    const std::optional<quint32> keyCountRead = QcStreamLimits::readCount(stream, 9);
    if (!keyCountRead) {
        return std::nullopt;
    }
    const quint32 keyCount = *keyCountRead;

    for (quint32 k = 0; k < keyCount; ++k) {
        QChar key;
        QString item;
        stream >> key >> item;
        recipe.keys.append({key, item});
    }

    std::optional<QStringList> ingredients = readRecipeStrings(stream);
    if (!ingredients) {
        return std::nullopt;
    }
    recipe.ingredients = std::move(*ingredients);

    stream >> recipe.resultItem >> resultCount;
    recipe.resultCount = resultCount;

    // Patterns must stay within a 3 x 3 grid and be rectangular, whatever the sender claims.
    const bool rectangular = std::ranges::all_of(recipe.pattern, [&recipe](const QString &row) {
        return row.size() == recipe.pattern.first().size() && row.size() <= 3;
    });
    const bool valid = stream.status() == QDataStream::Ok && recipe.pattern.size() <= 3
                       && !(recipe.shaped && (recipe.pattern.isEmpty() || !rectangular))
                       && !(!recipe.shaped && recipe.ingredients.isEmpty()) && recipe.ingredients.size() <= 9 && resultCount >= 1;
    return valid ? std::optional(std::move(recipe)) : std::nullopt;
}

[[nodiscard]] std::optional<QcTextureDefinition> readTexture(QDataStream &stream)
{
    QcTextureDefinition texture;

    QByteArray procedural;
    stream >> texture.name >> procedural >> texture.png;
    if (stream.status() != QDataStream::Ok) {
        return std::nullopt;
    }

    texture.procedural = QJsonDocument::fromJson(procedural).object();
    return texture;
}

[[nodiscard]] QcStatus readTranslations(QDataStream &stream, QcContentRegistry &registry)
{
    const std::optional<quint32> languageCountRead = QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions);
    if (!languageCountRead) {
        return qcError(u"Corrupt content: translations"_s);
    }
    const quint32 languageCount = *languageCountRead;

    quint32 total = 0;
    for (quint32 i = 0; i < languageCount; ++i) {
        QString language;
        stream >> language;
        const std::optional<quint32> entryCountRead = QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions);
        if (!entryCountRead || *entryCountRead > QcStreamLimits::maxTranslations - total) {
            return qcError(u"Corrupt content: translations"_s);
        }
        const quint32 entryCount = *entryCountRead;
        total += entryCount;

        QHash<QString, QString> entries;
        for (quint32 entry = 0; entry < entryCount; ++entry) {
            QString key;
            QString text;
            stream >> key >> text;
            entries.insert(key, text);
        }

        registry.addTranslations(language, entries);
    }
    return {};
}

[[nodiscard]] std::optional<QcPackReference> readPack(QDataStream &stream)
{
    QcPackReference pack;

    stream >> pack.id >> pack.name >> pack.version;
    const bool valid = !pack.id.isEmpty() && pack.id.size() <= 64 && pack.name.size() <= 256 && pack.version.size() <= 64;
    return valid ? std::optional(std::move(pack)) : std::nullopt;
}

[[nodiscard]] std::optional<QcModelDefinition> readModel(QDataStream &stream)
{
    QcModelDefinition model;

    stream >> model.name;
    const std::optional<quint32> boneCountRead = QcStreamLimits::readCount(stream, QcModelDefinition::maxBones);
    if (!boneCountRead) {
        return std::nullopt;
    }
    const quint32 boneCount = *boneCountRead;

    for (quint32 b = 0; b < boneCount; ++b) {
        QcModelBone bone;
        quint8 role = 0;
        stream >> bone.name >> role >> bone.pivot.x >> bone.pivot.y >> bone.pivot.z;
        if (role > quint8(QcBoneRole::LeftLeg) || !plausibleVector(bone.pivot)) {
            return std::nullopt;
        }
        std::optional<QList<QcModelBox>> boxes = readBoxes(stream, QcModelDefinition::maxBoxesPerBone);
        if (!boxes) {
            return std::nullopt;
        }
        bone.boxes = std::move(*boxes);

        bone.role = QcBoneRole(role);
        model.bones.append(std::move(bone));
    }
    return stream.status() == QDataStream::Ok ? std::optional(std::move(model)) : std::nullopt;
}

[[nodiscard]] std::optional<QcAppearanceDefinition> readAppearance(QDataStream &stream)
{
    QcAppearanceDefinition appearance;

    quint8 kind = 0;
    stream >> appearance.name >> appearance.translationKey >> kind >> appearance.playable;
    if (kind > quint8(QcAppearanceDefinition::Kind::Cape)) {
        return std::nullopt;
    }
    const std::optional<quint32> layerCountRead = QcStreamLimits::readCount(stream, QcAppearanceDefinition::maxLayers);
    if (!layerCountRead) {
        return std::nullopt;
    }
    const quint32 layerCount = *layerCountRead;

    appearance.kind = QcAppearanceDefinition::Kind(kind);

    for (quint32 l = 0; l < layerCount; ++l) {
        QcAppearanceLayer layer;
        quint8 tint = 0;
        stream >> layer.slot >> layer.texture >> tint;
        if (tint > quint8(QcTint::Pants)) {
            return std::nullopt;
        }
        layer.tint = QcTint(tint);
        appearance.layers.append(std::move(layer));
    }
    std::optional<QList<QcModelBox>> boxes = readBoxes(stream, QcAppearanceDefinition::maxBoxes);
    if (!boxes) {
        return std::nullopt;
    }
    appearance.boxes = std::move(*boxes);

    QcCapeShape &cape = appearance.cape;
    stream >> cape.width >> cape.length >> cape.thickness >> cape.offset;
    const bool shapeValid = cape.width >= QcCapeShape::minSize && cape.width <= QcCapeShape::maxWidth && cape.length >= QcCapeShape::minSize
                            && cape.length <= QcCapeShape::maxLength && cape.thickness > 0.0 && cape.thickness <= QcCapeShape::maxThickness
                            && cape.offset >= 0.0 && cape.offset <= QcCapeShape::maxOffset;
    return shapeValid && stream.status() == QDataStream::Ok ? std::optional(std::move(appearance)) : std::nullopt;
}

[[nodiscard]] std::optional<QcSmeltingRecipe> readSmeltingRecipe(QDataStream &stream)
{
    QcSmeltingRecipe recipe;

    qint32 resultCount = 0;
    stream >> recipe.name >> recipe.category >> recipe.inputItem >> recipe.resultItem >> resultCount >> recipe.seconds;
    recipe.resultCount = resultCount;
    const bool valid = stream.status() == QDataStream::Ok && resultCount >= 1 && resultCount <= 64 && std::isfinite(recipe.seconds)
                       && recipe.seconds >= QcSmeltingRecipe::minSeconds && recipe.seconds <= QcSmeltingRecipe::maxSeconds;
    return valid ? std::optional(std::move(recipe)) : std::nullopt;
}

[[nodiscard]] std::optional<QcSoundDefinition> readSound(QDataStream &stream)
{
    QcSoundDefinition sound;

    quint8 bus = 0;
    qint32 variants = 0;
    stream >> sound.name >> bus >> sound.volume >> sound.pitchVariation >> variants >> sound.loop >> sound.range;
    const std::optional<quint32> layerCountRead = QcStreamLimits::readCount(stream, QcSoundDefinition::maxLayers);
    if (!layerCountRead) {
        return std::nullopt;
    }
    const quint32 layerCount = *layerCountRead;

    sound.bus = QcSoundBus(bus);
    sound.variants = variants;

    for (quint32 l = 0; l < layerCount; ++l) {
        QcSoundLayer layer;
        quint8 wave = 0;
        stream >> wave >> layer.frequency >> layer.endFrequency >> layer.start >> layer.duration >> layer.attack >> layer.curve
            >> layer.gain >> layer.lowpass >> layer.highpass >> layer.tremoloRate >> layer.tremoloDepth >> layer.density;
        layer.wave = QcSoundWave(wave);
        sound.layers.append(layer);
    }

    // The synthesizer's work and memory follow from these values: the same limits as for content files.
    return stream.status() == QDataStream::Ok && sound.problem().isEmpty() ? std::optional(std::move(sound)) : std::nullopt;
}

[[nodiscard]] QcStatus readMobs(QDataStream &stream, QcContentRegistry &registry)
{
    return readTable(stream, QcStreamLimits::maxDefinitions, u"Corrupt content: mob table"_s, u"Corrupt content: mob %1"_s, [&] {
        std::optional<QcMobDefinition> definition = readMob(stream);
        if (!definition) {
            return false;
        }
        registry.registerMob(std::move(*definition));
        return true;
    });
}

[[nodiscard]] QcStatus readRecipes(QDataStream &stream, QcContentRegistry &registry)
{
    return readTable(stream, QcStreamLimits::maxDefinitions, u"Corrupt content: recipe table"_s, u"Corrupt content: recipe %1"_s, [&] {
        std::optional<QcRecipeDefinition> definition = readRecipe(stream);
        if (!definition) {
            return false;
        }
        registry.registerRecipe(std::move(*definition));
        return true;
    });
}

[[nodiscard]] QcStatus readTextures(QDataStream &stream, QcContentRegistry &registry)
{
    return readTable(stream, QcStreamLimits::maxDefinitions, u"Corrupt content: texture table"_s, u"Corrupt content: texture %1"_s, [&] {
        std::optional<QcTextureDefinition> definition = readTexture(stream);
        if (!definition) {
            return false;
        }
        registry.registerTexture(std::move(*definition));
        return true;
    });
}

[[nodiscard]] QcStatus readPacks(QDataStream &stream, QcContentRegistry &registry)
{
    return readTable(stream, 1024, u"Corrupt content: pack list"_s, u"Corrupt content: pack %1"_s, [&] {
        std::optional<QcPackReference> definition = readPack(stream);
        if (!definition) {
            return false;
        }
        registry.addPack(std::move(*definition));
        return true;
    });
}

[[nodiscard]] QcStatus readModels(QDataStream &stream, QcContentRegistry &registry)
{
    return readTable(stream, QcStreamLimits::maxDefinitions, u"Corrupt content: model table"_s, u"Corrupt content: model %1"_s, [&] {
        std::optional<QcModelDefinition> definition = readModel(stream);
        if (!definition) {
            return false;
        }
        registry.registerModel(std::move(*definition));
        return true;
    });
}

[[nodiscard]] QcStatus readAppearances(QDataStream &stream, QcContentRegistry &registry)
{
    return readTable(stream, QcStreamLimits::maxDefinitions, u"Corrupt content: appearance table"_s, u"Corrupt content: appearance %1"_s, [&] {
        std::optional<QcAppearanceDefinition> definition = readAppearance(stream);
        if (!definition) {
            return false;
        }
        registry.registerAppearance(std::move(*definition));
        return true;
    });
}

[[nodiscard]] QcStatus readSmeltingRecipes(QDataStream &stream, QcContentRegistry &registry)
{
    return readTable(stream, QcStreamLimits::maxDefinitions, u"Corrupt content: smelting recipes"_s, u"Corrupt content: smelting recipe %1"_s, [&] {
        std::optional<QcSmeltingRecipe> definition = readSmeltingRecipe(stream);
        if (!definition) {
            return false;
        }
        registry.registerSmeltingRecipe(std::move(*definition));
        return true;
    });
}

[[nodiscard]] QcStatus readSounds(QDataStream &stream, QcContentRegistry &registry)
{
    return readTable(stream, QcStreamLimits::maxDefinitions, u"Corrupt content: sound table"_s, u"Corrupt content: sound %1"_s, [&] {
        std::optional<QcSoundDefinition> definition = readSound(stream);
        if (!definition) {
            return false;
        }
        registry.registerSound(std::move(*definition));
        return true;
    });
}

void writeEffect(QDataStream &stream, const QcEffectDefinition &effect)
{
    stream << effect.name << effect.translationKey << effect.icon << effect.colour << effect.tintsHearts << effect.damage
           << qint32(effect.periodTicks) << effect.minHealth;
}

[[nodiscard]] QcStatus readEffects(QDataStream &stream, QcContentRegistry &registry)
{
    return readTable(stream, QcStreamLimits::maxDefinitions, u"Corrupt content: effect table"_s, u"Corrupt content: effect %1"_s, [&] {
        QcEffectDefinition effect;
        qint32 period = 0;
        stream >> effect.name >> effect.translationKey >> effect.icon >> effect.colour >> effect.tintsHearts >> effect.damage >> period
            >> effect.minHealth;
        effect.periodTicks = period;
        if (stream.status() != QDataStream::Ok || period < 1 || !std::isfinite(effect.damage) || !std::isfinite(effect.minHealth)) {
            return false;
        }
        registry.registerEffect(std::move(effect));
        return true;
    });
}

} // namespace

void QcContentSerializer::write(const QcContentRegistry &content, QDataStream &stream)
{
    writeList(stream, content.blocks(), [&content](QDataStream &out, const QcBlockDefinition &block) {
        QcBlockSerializer::writeBlock(out, block, content.shapeOf(block));
    });
    writeList(stream, content.items(), writeItem);
    writeList(stream, content.mobs(), writeMob);
    writeList(stream, content.recipes(), writeRecipe);
    writeList(stream, content.textures(), [](QDataStream &out, const QcTextureDefinition &texture) {
        out << texture.name << QJsonDocument(texture.procedural).toJson(QJsonDocument::Compact) << texture.png;
    });
    writeTranslations(stream, content.m_translations);
    writeList(stream, content.packs(), [](QDataStream &out, const QcPackReference &pack) {
        out << pack.id << pack.name << pack.version;
    });
    writeList(stream, content.models(), writeModel);
    writeList(stream, content.appearances(), writeAppearance);
    writeList(stream, content.smeltingRecipes(), [](QDataStream &out, const QcSmeltingRecipe &recipe) {
        out << recipe.name << recipe.category << recipe.inputItem << recipe.resultItem << qint32(recipe.resultCount)
            << recipe.seconds;
    });
    writeList(stream, content.sounds(), writeSound);
    writeList(stream, content.effects(), writeEffect);
}

QcStatus QcContentRegistry::readBlockTable(QDataStream &stream)
{
    m_blocks.clear();
    m_blockIds.clear();
    m_states.clear();

    const std::optional<quint32> blockCountRead = QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions);
    if (!blockCountRead || *blockCountRead == 0) {
        return qcError(u"Corrupt content: block table"_s);
    }
    const quint32 blockCount = *blockCountRead;

    for (quint32 i = 0; i < blockCount; ++i) {
        std::optional<QcBlockDefinition> read = QcBlockSerializer::readBlock(stream);
        if (!read || m_blockIds.contains(read->name)
            || m_states.size() + QcBlockStateTable::stateCountOf(*read) > QcBlockStateTable::maxStates
            || !m_states.fits(std::nullopt, QcBlockStateTable::derivedBoxes(*read, read->shape))) {
            return qcError(u"Corrupt content: block %1"_s.arg(i));
        }
        QcBlockDefinition block = std::move(*read);

        // The sender resolved the shape; the receiver has no shape table of its own.
        block.shapeName.clear();
        m_blockIds.insert(block.name, i);
        m_blocks.append(std::move(block));
        m_states.append(m_blocks.constLast(), m_blocks.constLast().shape);
    }

    if (m_blocks.constFirst().name != QLatin1StringView(airName)) {
        return qcError(u"Corrupt content: block 0 must be air"_s);
    }
    return {};
}

QcResult<QcContentRegistry> QcContentSerializer::read(QDataStream &stream)
{
    using ReadStep = QcStatus (*)(QDataStream &, QcContentRegistry &);
    // The order of the tables in the stream.
    constexpr ReadStep readBlocks = [](QDataStream &in, QcContentRegistry &registry) { return registry.readBlockTable(in); };
    constexpr std::array<ReadStep, 12> steps{readBlocks,       &readItems,           &readMobs,   &readRecipes,
                                             &readTextures,    &readTranslations,    &readPacks,  &readModels,
                                             &readAppearances, &readSmeltingRecipes, &readSounds, &readEffects};

    QcContentRegistry registry;
    for (const ReadStep step : steps) {
        if (const QcStatus status = step(stream, registry); !status) {
            return qcError(status.error());
        }
    }

    if (stream.status() != QDataStream::Ok) {
        return qcError(u"Corrupt content: truncated"_s);
    }
    return registry;
}
