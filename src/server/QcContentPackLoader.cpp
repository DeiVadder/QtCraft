#include "QcContentPackLoader.h"

#include "QcContentNames.h"
#include "QcContentParser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace Qt::StringLiterals;

namespace {

constexpr qint64 maxContentFileBytes = qint64(4) * 1024 * 1024;

QcResult<QJsonObject> readJsonObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return qcError(u"%1: cannot open (%2)"_s.arg(path, file.errorString()));
    }
    if (file.size() > maxContentFileBytes) {
        return qcError(u"%1: file too large"_s.arg(path));
    }
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
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
    const QDir dir(directory);
    const QStringList entries = dir.entryList({filter}, QDir::Files, QDir::Name);
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
    if (!QcContentNames::isValidPart(info.id) || info.id.contains(u'/')) {
        return qcError(u"%1: invalid or missing pack 'id'"_s.arg(directory));
    }
    if (info.version.isEmpty()) {
        return qcError(u"%1: missing 'version'"_s.arg(directory));
    }
    const QJsonValue dependencies = manifest->value(u"dependencies"_s);
    if (!dependencies.isUndefined() && !dependencies.isArray()) {
        return qcError(u"%1: 'dependencies' must be an array of pack ids"_s.arg(directory));
    }
    const QJsonArray dependencyList = dependencies.toArray();
    for (const auto &dependency : dependencyList) {
        if (!dependency.isString() || !QcContentNames::isValidPart(dependency.toString())) {
            return qcError(u"%1: invalid dependency entry"_s.arg(directory));
        }
        info.dependencies.append(dependency.toString());
    }
    return info;
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
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > maxContentFileBytes) {
            warnings.append(u"%1: cannot read texture"_s.arg(path));
            continue;
        }
        const QString name = QcContentNames::qualify(QFileInfo(path).completeBaseName(), info->id);
        if (name.isEmpty()) {
            warnings.append(u"%1: invalid texture file name"_s.arg(path));
            continue;
        }
        registry.registerTexture({name, {}, file.readAll()});
    }

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
    loadDefinitions(root.filePath(u"mobs"_s), info->id, warnings, QcContentParser::parseMob,
                    [&registry](QcMobDefinition mob) { registry.registerMob(std::move(mob)); });
    loadDefinitions(root.filePath(u"biomes"_s), info->id, warnings, QcContentParser::parseBiome,
                    [&registry](QcBiomeDefinition biome) { registry.registerBiome(std::move(biome)); });
    loadDefinitions(root.filePath(u"features"_s), info->id, warnings, QcContentParser::parseFeature,
                    [&registry](QcFeatureDefinition feature) { registry.registerFeature(std::move(feature)); });
    loadDefinitions(root.filePath(u"ores"_s), info->id, warnings, QcContentParser::parseOre,
                    [&registry](QcOreDefinition ore) { registry.registerOre(std::move(ore)); });
    const QString terrainPath = root.filePath(u"worldgen/terrain.json"_s);
    if (QFile::exists(terrainPath)) {
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
