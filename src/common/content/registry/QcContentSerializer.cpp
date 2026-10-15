#include "QcContentSerializer.h"

#include "QcArmourSerializer.h"
#include "QcBlockSerializer.h"
#include "QcContentRegistry.h"
#include "QcStreamLimits.h"

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

bool readBoxes(QDataStream &stream, quint32 limit, QList<QcModelBox> &boxes)
{
    quint32 count = 0;
    if (!QcStreamLimits::readCount(stream, limit, count)) {
        return false;
    }

    for (quint32 i = 0; i < count; ++i) {
        QcModelBox box;
        stream >> box.from.x >> box.from.y >> box.from.z >> box.to.x >> box.to.y >> box.to.z >> box.slot;
        if (stream.status() != QDataStream::Ok || !plausibleVector(box.from) || !plausibleVector(box.to)) {
            return false;
        }
        boxes.append(std::move(box));
    }
    return true;
}

void writeItem(QDataStream &stream, const QcItemDefinition &item)
{
    stream << item.name << item.translationKey << item.texture << qint32(item.maxStack) << item.placesBlock << item.tool
           << qint32(item.toolLevel) << item.toolSpeed << qint32(item.durability) << item.attackDamage << qint32(item.food)
           << item.fuelSeconds << quint32(item.tags.size());
    for (const QString &tag : item.tags) {
        stream << tag;
    }
    stream << item.scoops << item.emptiesTo << item.bottles << item.placesStand;
    QcArmourSerializer::write(stream, item.armour);
}

void writeMob(QDataStream &stream, const QcMobDefinition &mob)
{
    stream << mob.name << mob.translationKey << mob.maxHealth << mob.width << mob.height << mob.hostile
           << quint32(mob.parts.size());
    for (const QcMobPart &part : mob.parts) {
        stream << part.from.x << part.from.y << part.from.z << part.to.x << part.to.y << part.to.z << part.texture;
    }
    stream << mob.skeleton << mob.outfit << mob.cape << mob.armsForward << mob.ambientSound << mob.hurtSound << mob.deathSound
           << mob.flying << qint32(mob.glow) << mob.catchWith << mob.catchItem << quint32(mob.breeding.food.size());
    for (const QString &food : mob.breeding.food) {
        stream << food;
    }
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

void writeTranslations(QDataStream &stream, const QHash<QString, QHash<QString, QString>> &translations)
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
    quint32 count = 0;
    if (!QcStreamLimits::readCount(stream, limit, count)) {
        return qcError(tableError);
    }

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
    quint32 tagCount = 0;
    stream >> item.name >> item.translationKey >> item.texture >> maxStack >> item.placesBlock >> item.tool >> toolLevel
        >> item.toolSpeed >> durability >> item.attackDamage >> food >> item.fuelSeconds;
    if (!QcStreamLimits::readCount(stream, 16, tagCount)) {
        return false;
    }

    for (quint32 t = 0; t < tagCount; ++t) {
        QString tag;
        stream >> tag;
        item.tags.append(tag);
    }

    stream >> item.scoops >> item.emptiesTo >> item.bottles >> item.placesStand;
    if (!QcArmourSerializer::read(stream, item.armour)) {
        return false;
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

bool readMob(QDataStream &stream, QcMobDefinition &mob)
{
    quint32 partCount = 0;
    stream >> mob.name >> mob.translationKey >> mob.maxHealth >> mob.width >> mob.height >> mob.hostile;
    if (!QcStreamLimits::readCount(stream, 256, partCount)) {
        return false;
    }

    for (quint32 p = 0; p < partCount; ++p) {
        QcMobPart part;
        stream >> part.from.x >> part.from.y >> part.from.z >> part.to.x >> part.to.y >> part.to.z >> part.texture;
        mob.parts.append(part);
    }

    qint32 glow = 0;
    stream >> mob.skeleton >> mob.outfit >> mob.cape >> mob.armsForward >> mob.ambientSound >> mob.hurtSound >> mob.deathSound
        >> mob.flying >> glow >> mob.catchWith >> mob.catchItem;
    mob.glow = std::clamp(int(glow), 0, QcMobDefinition::maxGlow);
    // What it eats for breeding, so the client knows to use it on the creature (decision E-293).
    quint32 foodCount = 0;
    if (!QcStreamLimits::readCount(stream, 64, foodCount)) {
        return false;
    }
    for (quint32 f = 0; f < foodCount; ++f) {
        QString food;
        stream >> food;
        mob.breeding.food.append(food);
    }

    // Sizes feed physics and the mesher: reject anything outside what the parser allows.
    const auto plausible = [](double value) { return std::isfinite(value) && std::abs(value) <= 64.0; };
    bool valid = std::isfinite(mob.maxHealth) && mob.maxHealth > 0.0 && mob.maxHealth <= 1000.0 && plausible(mob.width) && plausible(mob.height) && mob.width > 0.0
                 && mob.height > 0.0;
    for (const QcMobPart &part : std::as_const(mob.parts)) {
        valid = valid && plausible(part.from.x) && plausible(part.from.y) && plausible(part.from.z)
                && plausible(part.to.x) && plausible(part.to.y) && plausible(part.to.z);
    }
    return stream.status() == QDataStream::Ok && valid;
}

// Bounded reads: QDataStream's own list operator would reserve whatever count arrives.
bool readRecipeStrings(QDataStream &stream, QList<QString> &list)
{
    quint32 count = 0;
    if (!QcStreamLimits::readCount(stream, 9, count)) {
        return false;
    }

    for (quint32 n = 0; n < count; ++n) {
        QString text;
        stream >> text;
        list.append(text);
    }
    return true;
}

bool readRecipe(QDataStream &stream, QcRecipeDefinition &recipe)
{
    quint32 keyCount = 0;
    qint32 resultCount = 0;
    stream >> recipe.name >> recipe.shaped;
    if (!readRecipeStrings(stream, recipe.pattern) || !QcStreamLimits::readCount(stream, 9, keyCount)) {
        return false;
    }

    for (quint32 k = 0; k < keyCount; ++k) {
        QChar key;
        QString item;
        stream >> key >> item;
        recipe.keys.append({key, item});
    }

    if (!readRecipeStrings(stream, recipe.ingredients)) {
        return false;
    }

    stream >> recipe.resultItem >> resultCount;
    recipe.resultCount = resultCount;

    // Patterns must stay within a 3 x 3 grid and be rectangular, whatever the sender claims.
    const bool rectangular = std::ranges::all_of(recipe.pattern, [&recipe](const QString &row) {
        return row.size() == recipe.pattern.first().size() && row.size() <= 3;
    });
    return stream.status() == QDataStream::Ok && recipe.pattern.size() <= 3
           && !(recipe.shaped && (recipe.pattern.isEmpty() || !rectangular)) && !(!recipe.shaped && recipe.ingredients.isEmpty())
           && recipe.ingredients.size() <= 9 && resultCount >= 1;
}

bool readTexture(QDataStream &stream, QcTextureDefinition &texture)
{
    QByteArray procedural;
    stream >> texture.name >> procedural >> texture.png;
    if (stream.status() != QDataStream::Ok) {
        return false;
    }

    texture.procedural = QJsonDocument::fromJson(procedural).object();
    return true;
}

[[nodiscard]] QcStatus readTranslations(QDataStream &stream, QcContentRegistry &registry)
{
    quint32 languageCount = 0;
    if (!QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions, languageCount)) {
        return qcError(u"Corrupt content: translations"_s);
    }

    quint32 total = 0;
    for (quint32 i = 0; i < languageCount; ++i) {
        QString language;
        quint32 entryCount = 0;
        stream >> language;
        if (!QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions, entryCount)
            || entryCount > QcStreamLimits::maxTranslations - total) {
            return qcError(u"Corrupt content: translations"_s);
        }
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

bool readPack(QDataStream &stream, QcPackReference &pack)
{
    stream >> pack.id >> pack.name >> pack.version;
    return !pack.id.isEmpty() && pack.id.size() <= 64 && pack.name.size() <= 256 && pack.version.size() <= 64;
}

bool readModel(QDataStream &stream, QcModelDefinition &model)
{
    quint32 boneCount = 0;
    stream >> model.name;
    if (!QcStreamLimits::readCount(stream, QcModelDefinition::maxBones, boneCount)) {
        return false;
    }

    for (quint32 b = 0; b < boneCount; ++b) {
        QcModelBone bone;
        quint8 role = 0;
        stream >> bone.name >> role >> bone.pivot.x >> bone.pivot.y >> bone.pivot.z;
        if (role > quint8(QcBoneRole::LeftLeg) || !plausibleVector(bone.pivot)
            || !readBoxes(stream, QcModelDefinition::maxBoxesPerBone, bone.boxes)) {
            return false;
        }

        bone.role = QcBoneRole(role);
        model.bones.append(std::move(bone));
    }
    return stream.status() == QDataStream::Ok;
}

bool readAppearance(QDataStream &stream, QcAppearanceDefinition &appearance)
{
    quint8 kind = 0;
    quint32 layerCount = 0;
    stream >> appearance.name >> appearance.translationKey >> kind >> appearance.playable;
    if (kind > quint8(QcAppearanceDefinition::Kind::Cape)
        || !QcStreamLimits::readCount(stream, QcAppearanceDefinition::maxLayers, layerCount)) {
        return false;
    }

    appearance.kind = QcAppearanceDefinition::Kind(kind);

    for (quint32 l = 0; l < layerCount; ++l) {
        QcAppearanceLayer layer;
        quint8 tint = 0;
        stream >> layer.slot >> layer.texture >> tint;
        if (tint > quint8(QcTint::Pants)) {
            return false;
        }
        layer.tint = QcTint(tint);
        appearance.layers.append(std::move(layer));
    }
    if (!readBoxes(stream, QcAppearanceDefinition::maxBoxes, appearance.boxes)) {
        return false;
    }

    QcCapeShape &cape = appearance.cape;
    stream >> cape.width >> cape.length >> cape.thickness >> cape.offset;
    const bool shapeValid = cape.width >= QcCapeShape::minSize && cape.width <= QcCapeShape::maxWidth && cape.length >= QcCapeShape::minSize
                            && cape.length <= QcCapeShape::maxLength && cape.thickness > 0.0 && cape.thickness <= QcCapeShape::maxThickness
                            && cape.offset >= 0.0 && cape.offset <= QcCapeShape::maxOffset;
    return shapeValid && stream.status() == QDataStream::Ok;
}

bool readSmeltingRecipe(QDataStream &stream, QcSmeltingRecipe &recipe)
{
    qint32 resultCount = 0;
    stream >> recipe.name >> recipe.category >> recipe.inputItem >> recipe.resultItem >> resultCount >> recipe.seconds;
    recipe.resultCount = resultCount;
    return stream.status() == QDataStream::Ok && resultCount >= 1 && resultCount <= 64 && std::isfinite(recipe.seconds)
           && recipe.seconds >= QcSmeltingRecipe::minSeconds && recipe.seconds <= QcSmeltingRecipe::maxSeconds;
}

bool readSound(QDataStream &stream, QcSoundDefinition &sound)
{
    quint8 bus = 0;
    qint32 variants = 0;
    quint32 layerCount = 0;
    stream >> sound.name >> bus >> sound.volume >> sound.pitchVariation >> variants >> sound.loop >> sound.range;
    if (!QcStreamLimits::readCount(stream, QcSoundDefinition::maxLayers, layerCount)) {
        return false;
    }

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
    return stream.status() == QDataStream::Ok && sound.problem().isEmpty();
}

[[nodiscard]] QcStatus readMobs(QDataStream &stream, QcContentRegistry &registry)
{
    return readTable(stream, QcStreamLimits::maxDefinitions, u"Corrupt content: mob table"_s, u"Corrupt content: mob %1"_s, [&] {
        QcMobDefinition definition;
        if (!readMob(stream, definition)) {
            return false;
        }
        registry.registerMob(std::move(definition));
        return true;
    });
}

[[nodiscard]] QcStatus readRecipes(QDataStream &stream, QcContentRegistry &registry)
{
    return readTable(stream, QcStreamLimits::maxDefinitions, u"Corrupt content: recipe table"_s, u"Corrupt content: recipe %1"_s, [&] {
        QcRecipeDefinition definition;
        if (!readRecipe(stream, definition)) {
            return false;
        }
        registry.registerRecipe(std::move(definition));
        return true;
    });
}

[[nodiscard]] QcStatus readTextures(QDataStream &stream, QcContentRegistry &registry)
{
    return readTable(stream, QcStreamLimits::maxDefinitions, u"Corrupt content: texture table"_s, u"Corrupt content: texture %1"_s, [&] {
        QcTextureDefinition definition;
        if (!readTexture(stream, definition)) {
            return false;
        }
        registry.registerTexture(std::move(definition));
        return true;
    });
}

[[nodiscard]] QcStatus readPacks(QDataStream &stream, QcContentRegistry &registry)
{
    return readTable(stream, 1024, u"Corrupt content: pack list"_s, u"Corrupt content: pack %1"_s, [&] {
        QcPackReference definition;
        if (!readPack(stream, definition)) {
            return false;
        }
        registry.addPack(std::move(definition));
        return true;
    });
}

[[nodiscard]] QcStatus readModels(QDataStream &stream, QcContentRegistry &registry)
{
    return readTable(stream, QcStreamLimits::maxDefinitions, u"Corrupt content: model table"_s, u"Corrupt content: model %1"_s, [&] {
        QcModelDefinition definition;
        if (!readModel(stream, definition)) {
            return false;
        }
        registry.registerModel(std::move(definition));
        return true;
    });
}

[[nodiscard]] QcStatus readAppearances(QDataStream &stream, QcContentRegistry &registry)
{
    return readTable(stream, QcStreamLimits::maxDefinitions, u"Corrupt content: appearance table"_s, u"Corrupt content: appearance %1"_s, [&] {
        QcAppearanceDefinition definition;
        if (!readAppearance(stream, definition)) {
            return false;
        }
        registry.registerAppearance(std::move(definition));
        return true;
    });
}

[[nodiscard]] QcStatus readSmeltingRecipes(QDataStream &stream, QcContentRegistry &registry)
{
    return readTable(stream, QcStreamLimits::maxDefinitions, u"Corrupt content: smelting recipes"_s, u"Corrupt content: smelting recipe %1"_s, [&] {
        QcSmeltingRecipe definition;
        if (!readSmeltingRecipe(stream, definition)) {
            return false;
        }
        registry.registerSmeltingRecipe(std::move(definition));
        return true;
    });
}

[[nodiscard]] QcStatus readSounds(QDataStream &stream, QcContentRegistry &registry)
{
    return readTable(stream, QcStreamLimits::maxDefinitions, u"Corrupt content: sound table"_s, u"Corrupt content: sound %1"_s, [&] {
        QcSoundDefinition definition;
        if (!readSound(stream, definition)) {
            return false;
        }
        registry.registerSound(std::move(definition));
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
}

QcStatus QcContentSerializer::readBlocks(QDataStream &stream, QcContentRegistry &registry)
{
    registry.m_blocks.clear();
    registry.m_blockIds.clear();
    registry.m_states.clear();

    quint32 blockCount = 0;
    if (!QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions, blockCount) || blockCount == 0) {
        return qcError(u"Corrupt content: block table"_s);
    }

    for (quint32 i = 0; i < blockCount; ++i) {
        QcBlockDefinition block;
        if (!QcBlockSerializer::readBlock(stream, block) || registry.m_blockIds.contains(block.name)
            || registry.m_states.size() + QcBlockStateTable::stateCountOf(block) > QcBlockStateTable::maxStates
            || !registry.m_states.fits(std::nullopt, QcBlockStateTable::derivedBoxes(block, block.shape))) {
            return qcError(u"Corrupt content: block %1"_s.arg(i));
        }

        // The sender resolved the shape; the receiver has no shape table of its own.
        block.shapeName.clear();
        registry.m_blockIds.insert(block.name, i);
        registry.m_blocks.append(std::move(block));
        registry.m_states.append(registry.m_blocks.constLast(), registry.m_blocks.constLast().shape);
    }

    if (registry.m_blocks.constFirst().name != QLatin1StringView(QcContentRegistry::airName)) {
        return qcError(u"Corrupt content: block 0 must be air"_s);
    }
    return {};
}

QcResult<QcContentRegistry> QcContentSerializer::read(QDataStream &stream)
{
    using ReadStep = QcStatus (*)(QDataStream &, QcContentRegistry &);
    // The order of the tables in the stream.
    constexpr std::array<ReadStep, 11> steps{&readBlocks, &readItems,  &readMobs,        &readRecipes,
                                             &readTextures, &readTranslations, &readPacks, &readModels,
                                             &readAppearances, &readSmeltingRecipes, &readSounds};

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
