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
        files.append(dir.filePath(entry));
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

qint64 QcContentPackLoader::measure(const QString &directory)
{
    const QDir root(directory);
    QStringList files;
    for (const char *folder :
         {"textures", "shapes", "parts", "materials", "blocks", "items", "recipes", "smelting", "mobs", "models", "appearance", "biomes", "features", "ores", "lang", "sounds"}) {
        files += filesIn(root.filePath(QString::fromLatin1(folder)), u"*.json"_s);
    }
    files += filesIn(root.filePath(u"textures"_s), u"*.png"_s);
    if (!QFileInfo(root.filePath(u"worldgen"_s)).isSymLink()) {
        files.append(root.filePath(u"worldgen/terrain.json"_s));
    }
    qint64 total = 0;
    for (const QString &path : std::as_const(files)) {
        const QFileInfo info(path);
        if (info.isFile() && !info.isSymLink()) {
            total += std::max<qint64>(256, std::min(info.size(), maxContentFileBytes + 1));
        }
    }
    return total;
}

QcResult<QcContentPackInfo> QcContentPackLoader::load(const QString &directory, QcContentRegistry &registry,
                                                      QStringList &warnings)
{
    QcResult<QcContentPackInfo> info = readManifest(directory);
    if (!info) {
        return info;
    }
    const QDir root(directory);

    const QStringList textureFiles = filesIn(root.filePath(u"textures"_s), u"*.json"_s);
    for (const QString &path : textureFiles) {
        const QcResult<QJsonObject> json = readJsonObject(path);
        const QcResult<QcTextureDefinition> texture =
            json ? QcContentParser::parseProceduralTexture(*json, info->id) : QcResult<QcTextureDefinition>(qcError(json.error()));
        if (!texture) {
            warnings.append(u"%1: %2"_s.arg(path, texture.error()));
            continue;
        }
        registry.registerTexture(*texture);
    }
    const QStringList pngFiles = filesIn(root.filePath(u"textures"_s), u"*.png"_s);
    for (const QString &path : pngFiles) {
        const QcResult<QByteArray> png = readPackFile(path);
        if (!png) {
            warnings.append(png.error());
            continue;
        }
        const QString name = QcContentNames::qualify(QFileInfo(path).completeBaseName(), info->id);
        if (name.isEmpty()) {
            warnings.append(u"%1: invalid texture file name"_s.arg(path));
            continue;
        }
        registry.registerTexture({name, {}, *png});
    }

    loadDefinitions(root.filePath(u"shapes"_s), info->id, warnings, QcContentParser::parseShape,
                    [&registry, &warnings](QcShapeDefinition shape) {
                        if (const QcStatus registered = registry.registerShape(std::move(shape)); !registered) {
                            warnings.append(registered.error());
                        }
                    });
    const QStringList blockFiles = filesIn(root.filePath(u"blocks"_s), u"*.json"_s);
    for (const QString &path : blockFiles) {
        const QcResult<QJsonObject> json = readJsonObject(path);
        if (!json) {
            warnings.append(json.error());
            continue;
        }
        QcResult<QcBlockDefinition> block = QcContentParser::parseBlock(*json, info->id);
        if (!block) {
            warnings.append(u"%1: %2"_s.arg(path, block.error()));
            continue;
        }
        const QcResult<QcBlockId> id = registry.registerBlock(std::move(*block));
        if (!id) {
            warnings.append(u"%1: %2"_s.arg(path, id.error()));
        }
    }

    loadDefinitions(root.filePath(u"items"_s), info->id, warnings, QcContentParser::parseItem,
                    [&registry, &warnings](QcItemDefinition item) {
                        const QString name = item.name;
                        if (const QcResult<QcItemId> id = registry.registerItem(std::move(item)); !id) {
                            warnings.append(u"item %1: %2"_s.arg(name, id.error()));
                        }
                    });
    loadDefinitions(root.filePath(u"recipes"_s), info->id, warnings, QcContentParser::parseRecipe,
                    [&registry](QcRecipeDefinition recipe) { registry.registerRecipe(std::move(recipe)); });
    loadDefinitions(root.filePath(u"smelting"_s), info->id, warnings, QcContentParser::parseSmeltingRecipe,
                    [&registry](QcSmeltingRecipe recipe) { registry.registerSmeltingRecipe(std::move(recipe)); });
    loadDefinitions(root.filePath(u"parts"_s), info->id, warnings, QcContentParser::parsePartFamily,
                    [&registry](QcPartFamily family) { registry.registerPartFamily(std::move(family)); });
    loadDefinitions(root.filePath(u"materials"_s), info->id, warnings, QcContentParser::parseMaterial,
                    [&registry](QcMaterialDefinition material) { registry.registerMaterial(std::move(material)); });
    loadDefinitions(root.filePath(u"mobs"_s), info->id, warnings, QcContentParser::parseMob,
                    [&registry](QcMobDefinition mob) { registry.registerMob(std::move(mob)); });
    loadDefinitions(root.filePath(u"models"_s), info->id, warnings, QcContentParser::parseModel,
                    [&registry](QcModelDefinition model) { registry.registerModel(std::move(model)); });
    loadDefinitions(root.filePath(u"appearance"_s), info->id, warnings, QcContentParser::parseAppearance,
                    [&registry](QcAppearanceDefinition appearance) { registry.registerAppearance(std::move(appearance)); });
    loadDefinitions(root.filePath(u"sounds"_s), info->id, warnings, QcContentParser::parseSound,
                    [&registry](QcSoundDefinition sound) { registry.registerSound(std::move(sound)); });
    loadDefinitions(root.filePath(u"biomes"_s), info->id, warnings, QcContentParser::parseBiome,
                    [&registry](QcBiomeDefinition biome) { registry.registerBiome(std::move(biome)); });
    loadDefinitions(root.filePath(u"features"_s), info->id, warnings, QcContentParser::parseFeature,
                    [&registry](QcFeatureDefinition feature) { registry.registerFeature(std::move(feature)); });
    loadDefinitions(root.filePath(u"ores"_s), info->id, warnings, QcContentParser::parseOre,
                    [&registry](QcOreDefinition ore) { registry.registerOre(std::move(ore)); });
    const QString terrainPath = root.filePath(u"worldgen/terrain.json"_s);
    if (!QFileInfo(root.filePath(u"worldgen"_s)).isSymLink() && QFileInfo::exists(terrainPath)) {
        const QcResult<QJsonObject> json = readJsonObject(terrainPath);
        const QcResult<QcTerrainSettings> terrain =
            json ? QcContentParser::parseTerrainSettings(*json, info->id) : QcResult<QcTerrainSettings>(qcError(json.error()));
        if (terrain) {
            registry.setTerrainSettings(*terrain);
        } else {
            warnings.append(u"%1: %2"_s.arg(terrainPath, terrain.error()));
        }
    }

    const QStringList languageFiles = filesIn(root.filePath(u"lang"_s), u"*.json"_s);
    for (const QString &path : languageFiles) {
        const QcResult<QJsonObject> json = readJsonObject(path);
        const QcResult<QHash<QString, QString>> entries =
            json ? QcContentParser::parseTranslations(*json) : QcResult<QHash<QString, QString>>(qcError(json.error()));
        if (!entries) {
            warnings.append(u"%1: %2"_s.arg(path, entries.error()));
            continue;
        }
        registry.addTranslations(QFileInfo(path).completeBaseName(), *entries);
    }
    return info;
}
