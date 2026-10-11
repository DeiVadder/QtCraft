#include "QcContentPackLoader.h"

#include "QcContentNames.h"
#include "QcContentParser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <optional>
#include <type_traits>

using namespace Qt::StringLiterals;

QcResult<QByteArray> QcContentPackLoader::readPackFile(const QString &path)
{
    // Only plain files: devices and pipes report size 0 and never end, links could leave the pack.
    const QFileInfo info(path);
    if (info.isSymLink() || !info.isFile()) {
        return qcError(u"%1: not a regular file"_s.arg(path));
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return qcError(u"%1: cannot open (%2)"_s.arg(path, file.errorString()));
    }

    QByteArray data = file.read(maxContentFileBytes + 1);
    if (data.size() > maxContentFileBytes) {
        return qcError(u"%1: file too large"_s.arg(path));
    }

    return data;
}

namespace {

QcResult<QJsonObject> readJsonObject(const QString &path)
{
    const QcResult<QByteArray> data = QcContentPackLoader::readPackFile(path);
    if (!data) {
        return qcError(data.error());
    }

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(*data, &error);
    if (error.error != QJsonParseError::NoError) {
        return qcError(u"%1: %2 at offset %3"_s.arg(path, error.errorString()).arg(error.offset));
    }

    if (!document.isObject()) {
        return qcError(u"%1: top level must be a JSON object"_s.arg(path));
    }
    return document.object();
}

QStringList filesIn(const QString &directory, const QString &filter)
{
    QStringList files;
    if (QFileInfo(directory).isSymLink()) {
        return files;
    }

    const QDir dir(directory);
    const QStringList entries = dir.entryList({filter}, QDir::Files | QDir::NoSymLinks, QDir::Name);
    for (const QString &entry : entries) {
        // Hidden files are no content: macOS leaves "._name.json" beside files copied to other disks.
        if (!entry.startsWith(u'.')) {
            files.append(dir.filePath(entry));
        }
    }
    return files;
}

template<typename Parser, typename Register>
void loadDefinitions(const QString &directory, const QString &packNamespace, QStringList &warnings, Parser parse,
                     Register registerDefinition)
{
    const QStringList files = filesIn(directory, u"*.json"_s);
    for (const QString &path : files) {
        const QcResult<QJsonObject> json = readJsonObject(path);
        if (!json) {
            warnings.append(json.error());
            continue;
        }

        auto definition = parse(*json, packNamespace);
        if (!definition) {
            warnings.append(u"%1: %2"_s.arg(path, definition.error()));
            continue;
        }

        registerDefinition(std::move(*definition));
    }
}

} // namespace

bool QcContentPackLoader::isValidPackId(const QString &id)
{
    static const QRegularExpression pattern(u"^[a-z0-9][a-z0-9_.-]{0,63}$"_s);
    return pattern.match(id).hasMatch() && !id.contains(u".."_s);
}

QVersionNumber QcContentPackLoader::parseVersion(const QString &text)
{
    qsizetype suffix = 0;
    const QVersionNumber version = QVersionNumber::fromString(text, &suffix);
    // "1.0" and "1.0.0" are the same version; trailing text such as "1.0beta" is not accepted.
    return suffix == text.size() && !version.isNull() ? version.normalized() : QVersionNumber();
}

QcResult<QcContentPackInfo> QcContentPackLoader::readManifest(const QString &directory)
{
    const QcResult<QJsonObject> manifest = readJsonObject(QDir(directory).filePath(u"pack.json"_s));
    if (!manifest) {
        return qcError(manifest.error());
    }

    QcContentPackInfo info;
    info.directory = directory;
    info.id = manifest->value(u"id"_s).toString();
    info.name = manifest->value(u"name"_s).toString(info.id);
    info.version = manifest->value(u"version"_s).toString();
    info.description = manifest->value(u"description"_s).toString();

    if (info.name.size() > 256 || info.version.size() > 64 || info.description.size() > 4096) {
        return qcError(u"%1: 'name', 'version' or 'description' is too long"_s.arg(directory));
    }
    if (!isValidPackId(info.id)) {
        return qcError(u"%1: invalid or missing pack 'id'"_s.arg(directory));
    }
    if (parseVersion(info.version).isNull()) {
        return qcError(u"%1: missing or invalid 'version' (expected e.g. \"1.0.0\")"_s.arg(directory));
    }

    const QJsonValue dependencies = manifest->value(u"dependencies"_s);
    if (!dependencies.isUndefined() && !dependencies.isArray()) {
        return qcError(u"%1: 'dependencies' must be an array of pack ids"_s.arg(directory));
    }
    const QJsonArray dependencyList = dependencies.toArray();
    if (dependencyList.size() > 64) {
        return qcError(u"%1: too many dependencies"_s.arg(directory));
    }

    // Either "id" or {"id": "...", "minVersion": "1.2"}.
    for (const auto &dependency : dependencyList) {
        const QJsonObject object = dependency.toObject();
        const QString id = dependency.isString() ? dependency.toString() : object.value(u"id"_s).toString();
        if (!isValidPackId(id) || id == info.id || info.dependencies.contains(id)) {
            return qcError(u"%1: invalid dependency entry '%2'"_s.arg(directory, id));
        }

        info.dependencies.append(id);
        if (object.contains(u"minVersion"_s)) {
            const QVersionNumber minimum = parseVersion(object.value(u"minVersion"_s).toString());
            if (minimum.isNull()) {
                return qcError(u"%1: invalid minVersion for dependency '%2'"_s.arg(directory, id));
            }
            info.minimumVersions.insert(id, minimum);
        }
    }

    return info;
}

QStringList QcContentPackLoader::contentFiles(const QString &directory)
{
    const QDir root(directory);
    QStringList files;
    for (const char *folder :
         {"textures", "shapes", "parts", "materials", "blocks", "items", "recipes", "smelting", "mobs", "models", "appearance", "biomes", "features", "ores", "lang", "sounds", "handbook", "structures", "commands"}) {
        files += filesIn(root.filePath(QString::fromLatin1(folder)), u"*.json"_s);
    }

    files += filesIn(root.filePath(u"textures"_s), u"*.png"_s);
    if (!QFileInfo(root.filePath(u"worldgen"_s)).isSymLink()) {
        for (const char *name : {"terrain.json", "showcase.json", "flat.json", "void.json"}) {
            files.append(root.filePath(u"worldgen/"_s + QString::fromLatin1(name)));
        }
    }

    // Only what exists as a regular file is read.
    files.removeIf([](const QString &path) {
        const QFileInfo info(path);
        return !info.isFile() || info.isSymLink();
    });
    files.sort();
    return files;
}

qint64 QcContentPackLoader::measure(const QString &directory)
{
    qint64 total = 0;
    const QStringList files = contentFiles(directory);
    for (const QString &path : files) {
        total += std::max<qint64>(256, std::min(QFileInfo(path).size(), maxContentFileBytes + 1));
    }
    return total;
}

namespace {

void loadTextures(const QDir &root, const QString &packId, QcContentRegistry &registry, QStringList &warnings)
{
    const QStringList textureFiles = filesIn(root.filePath(u"textures"_s), u"*.json"_s);
    for (const QString &path : textureFiles) {
        const QcResult<QJsonObject> json = readJsonObject(path);
        const QcResult<QcTextureDefinition> texture =
            json ? QcContentParser::parseProceduralTexture(*json, packId) : QcResult<QcTextureDefinition>(qcError(json.error()));
        if (!texture) {
            warnings.append(u"%1: %2"_s.arg(path, texture.error()));
            continue;
        }

        registry.registerTexture(*texture);
    }

    const QStringList pngFiles = filesIn(root.filePath(u"textures"_s), u"*.png"_s);
    for (const QString &path : pngFiles) {
        const QcResult<QByteArray> png = QcContentPackLoader::readPackFile(path);
        if (!png) {
            warnings.append(png.error());
            continue;
        }

        const QString name = QcContentNames::qualify(QFileInfo(path).completeBaseName(), packId);
        if (name.isEmpty()) {
            warnings.append(u"%1: invalid texture file name"_s.arg(path));
            continue;
        }

        registry.registerTexture({name, {}, *png});
    }
}

void loadBlocks(const QDir &root, const QString &packId, QcContentRegistry &registry, QStringList &warnings)
{
    const QStringList blockFiles = filesIn(root.filePath(u"blocks"_s), u"*.json"_s);
    for (const QString &path : blockFiles) {
        const QcResult<QJsonObject> json = readJsonObject(path);
        if (!json) {
            warnings.append(json.error());
            continue;
        }

        QcResult<QcBlockDefinition> block = QcContentParser::parseBlock(*json, packId);
        if (!block) {
            warnings.append(u"%1: %2"_s.arg(path, block.error()));
            continue;
        }

        const QcResult<QcBlockId> id = registry.registerBlock(std::move(*block));
        if (!id) {
            warnings.append(u"%1: %2"_s.arg(path, id.error()));
        }
    }
}

// Definition folders loaded after the blocks, in this order.
void loadLaterDefinitions(const QDir &root, const QString &packId, QcContentRegistry &registry, QStringList &warnings)
{
    loadDefinitions(root.filePath(u"items"_s), packId, warnings, QcContentParser::parseItem,
                    [&registry, &warnings](QcItemDefinition item) {
                        const QString name = item.name;
                        if (const QcResult<QcItemId> id = registry.registerItem(std::move(item)); !id) {
                            warnings.append(u"item %1: %2"_s.arg(name, id.error()));
                        }
                    });

    loadDefinitions(root.filePath(u"recipes"_s), packId, warnings, QcContentParser::parseRecipe,
                    [&registry](QcRecipeDefinition recipe) { registry.registerRecipe(std::move(recipe)); });
    loadDefinitions(root.filePath(u"smelting"_s), packId, warnings, QcContentParser::parseSmeltingRecipe,
                    [&registry](QcSmeltingRecipe recipe) { registry.registerSmeltingRecipe(std::move(recipe)); });
    loadDefinitions(root.filePath(u"parts"_s), packId, warnings, QcContentParser::parsePartFamily,
                    [&registry](QcPartFamily family) { registry.registerPartFamily(std::move(family)); });
    loadDefinitions(root.filePath(u"materials"_s), packId, warnings, QcContentParser::parseMaterial,
                    [&registry](QcMaterialDefinition material) { registry.registerMaterial(std::move(material)); });
    loadDefinitions(root.filePath(u"mobs"_s), packId, warnings, QcContentParser::parseMob,
                    [&registry](QcMobDefinition mob) { registry.registerMob(std::move(mob)); });
    loadDefinitions(root.filePath(u"models"_s), packId, warnings, QcContentParser::parseModel,
                    [&registry](QcModelDefinition model) { registry.registerModel(std::move(model)); });
    loadDefinitions(root.filePath(u"appearance"_s), packId, warnings, QcContentParser::parseAppearance,
                    [&registry](QcAppearanceDefinition appearance) { registry.registerAppearance(std::move(appearance)); });
    loadDefinitions(root.filePath(u"sounds"_s), packId, warnings, QcContentParser::parseSound,
                    [&registry](QcSoundDefinition sound) { registry.registerSound(std::move(sound)); });
    loadDefinitions(root.filePath(u"biomes"_s), packId, warnings, QcContentParser::parseBiome,
                    [&registry](QcBiomeDefinition biome) { registry.registerBiome(std::move(biome)); });
    loadDefinitions(root.filePath(u"features"_s), packId, warnings, QcContentParser::parseFeature,
                    [&registry](QcFeatureDefinition feature) { registry.registerFeature(std::move(feature)); });
    loadDefinitions(root.filePath(u"ores"_s), packId, warnings, QcContentParser::parseOre,
                    [&registry](QcOreDefinition ore) { registry.registerOre(std::move(ore)); });
    loadDefinitions(root.filePath(u"handbook"_s), packId, warnings, QcContentParser::parseHandbookChapter,
                    [&registry](QcHandbookChapter chapter) { registry.registerHandbookChapter(std::move(chapter)); });
    loadDefinitions(root.filePath(u"structures"_s), packId, warnings, QcContentParser::parseStructure,
                    [&registry](QcStructureDefinition structure) { registry.registerStructure(std::move(structure)); });
    loadDefinitions(root.filePath(u"commands"_s), packId, warnings, QcContentParser::parseCommandMacro,
                    [&registry](QcCommandMacroDefinition macro) { registry.registerCommandMacro(std::move(macro)); });
}

// worldgen/<file>: one settings object of the pack, parsed; std::nullopt (with a warning when it
// is broken) when there is none.
template<typename Parse>
auto readWorldgenFile(const QDir &root, const QString &file, const QString &packId, QStringList &warnings, Parse parse)
    -> std::optional<typename std::invoke_result_t<Parse, const QJsonObject &, const QString &>::value_type>
{
    const QString path = root.filePath(u"worldgen/"_s + file);
    if (QFileInfo(root.filePath(u"worldgen"_s)).isSymLink() || !QFileInfo::exists(path)) {
        return std::nullopt;
    }

    const QcResult<QJsonObject> json = readJsonObject(path);
    if (!json) {
        warnings.append(u"%1: %2"_s.arg(path, json.error()));
        return std::nullopt;
    }

    auto parsed = parse(*json, packId);
    if (!parsed) {
        warnings.append(u"%1: %2"_s.arg(path, parsed.error()));
        return std::nullopt;
    }
    return std::move(*parsed);
}

void loadWorldgenSettings(const QDir &root, const QString &packId, QcContentRegistry &registry, QStringList &warnings)
{
    if (auto terrain = readWorldgenFile(root, u"terrain.json"_s, packId, warnings, QcContentParser::parseTerrainSettings)) {
        registry.setTerrainSettings(std::move(*terrain));
    }
    if (auto showcase = readWorldgenFile(root, u"showcase.json"_s, packId, warnings, QcContentParser::parseShowcase)) {
        registry.setShowcase(std::move(*showcase));
    }
    if (auto flat = readWorldgenFile(root, u"flat.json"_s, packId, warnings, QcContentParser::parseFlat)) {
        registry.setFlatWorld(std::move(*flat));
    }
    if (auto empty = readWorldgenFile(root, u"void.json"_s, packId, warnings, QcContentParser::parseVoid)) {
        registry.setVoidWorld(std::move(*empty));
    }
}

void loadTranslations(const QDir &root, QcContentRegistry &registry, QStringList &warnings)
{
    const QStringList languageFiles = filesIn(root.filePath(u"lang"_s), u"*.json"_s);
    for (const QString &path : languageFiles) {
        const QcResult<QJsonObject> json = readJsonObject(path);
        const QcResult<QHash<QString, QString>> entries =
            json ? QcContentParser::parseTranslations(*json) : QcResult<QHash<QString, QString>>(qcError(json.error()));
        if (!entries) {
            warnings.append(u"%1: %2"_s.arg(path, entries.error()));
            continue;
        }

        // "de.json" and "de.handbook.json" both hold German texts; other dots belong to the language.
        QString language = QFileInfo(path).completeBaseName();
        const QLatin1StringView suffix(QcContentPackLoader::handbookLanguageSuffix);
        if (language.endsWith(suffix)) {
            language.chop(suffix.size());
        }
        registry.addTranslations(language, *entries);
    }
}

} // namespace

QcResult<QcContentPackInfo> QcContentPackLoader::load(const QString &directory, QcContentRegistry &registry,
                                                      QStringList &warnings)
{
    QcResult<QcContentPackInfo> info = readManifest(directory);
    if (!info) {
        return info;
    }

    const QDir root(directory);
    loadTextures(root, info->id, registry, warnings);
    loadDefinitions(root.filePath(u"shapes"_s), info->id, warnings, QcContentParser::parseShape,
                    [&registry, &warnings](QcShapeDefinition shape) {
                        if (const QcStatus registered = registry.registerShape(std::move(shape)); !registered) {
                            warnings.append(registered.error());
                        }
                    });
    loadBlocks(root, info->id, registry, warnings);
    loadLaterDefinitions(root, info->id, registry, warnings);
    loadWorldgenSettings(root, info->id, registry, warnings);
    loadTranslations(root, registry, warnings);

    return info;
}
