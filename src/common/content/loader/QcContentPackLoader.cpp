#include "common/content/loader/QcContentPackLoader.h"

#include "common/content/definitions/QcTextureAnimation.h"
#include "common/content/parser/QcContentParser.h"
#include "common/content/registry/QcContentNames.h"
#include "common/protocol/QcStreamLimits.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <optional>
#include <utility>
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

[[nodiscard]] QcResult<QJsonObject> readJsonObject(const QString &path)
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
         {"textures", "shapes", "parts", "materials", "blocks", "items", "recipes", "smelting", "stonecutting", "mobs", "models", "appearance", "biomes", "features", "ores", "lang", "sounds", "handbook", "structures", "commands", "villages", "trades", "professions", "villagers", "projectiles", "effects", "loot"}) {
        files += filesIn(root.filePath(QString::fromLatin1(folder)), u"*.json"_s);
    }

    files += filesIn(root.filePath(u"textures"_s), u"*.png"_s);
    if (!QFileInfo(root.filePath(u"worldgen"_s)).isSymLink()) {
        for (const char *name : {"terrain.json", "showcase.json", "flat.json", "void.json", "villages.json"}) {
            files.append(root.filePath(u"worldgen/"_s + QString::fromLatin1(name)));
        }
    }
    files.append(root.filePath(u"weather.json"_s));
    files.append(root.filePath(u"held.json"_s));

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

// Loads the content folders of one pack into a registry and collects the warnings about the
// entries it leaves out.
class PackReader
{
public:
    PackReader(const QString &directory, QString packId, QcContentRegistry &registry)
        : m_root(directory)
        , m_packId(std::move(packId))
        , m_registry(registry)
    {
    }

    void loadTextures();
    void loadShapes();
    void loadBlocks();
    void loadLaterDefinitions();
    void loadWorldgenSettings();
    void loadWeatherSettings();
    // How items are held by category (`held.json`, decision E-831).
    void loadHoldDefaults();
    void loadTranslations();

    [[nodiscard]] const QStringList &warnings() const { return m_warnings; }

private:
    template<typename Parser, typename Register>
    void loadDefinitions(const QString &folder, Parser parse, Register registerDefinition);
    template<typename Parse>
    [[nodiscard]] auto readSettingsFile(const QString &file, Parse parse)
        -> std::optional<typename std::invoke_result_t<Parse, const QJsonObject &, const QString &>::value_type>;
    void loadVillageStyles();
    void applyStyleExtensions(const QList<std::pair<QString, QJsonObject>> &extensions);

    QDir m_root;
    QString m_packId;
    QcContentRegistry &m_registry;
    QStringList m_warnings;
};

// The definitions in <folder>/*.json, each parsed and handed to `registerDefinition`; a register
// function that returns a QcStatus reports a refused definition as a warning.
template<typename Parser, typename Register>
void PackReader::loadDefinitions(const QString &folder, Parser parse, Register registerDefinition)
{
    const QStringList files = filesIn(m_root.filePath(folder), u"*.json"_s);
    for (const QString &path : files) {
        const QcResult<QJsonObject> json = readJsonObject(path);
        if (!json) {
            m_warnings.append(json.error());
            continue;
        }

        auto definition = parse(*json, m_packId);
        if (!definition) {
            m_warnings.append(u"%1: %2"_s.arg(path, definition.error()));
            continue;
        }

        using Registered = std::invoke_result_t<Register, decltype(std::move(*definition))>;
        if constexpr (std::is_void_v<Registered>) {
            registerDefinition(std::move(*definition));
        } else if (const Registered registered = registerDefinition(std::move(*definition)); !registered) {
            m_warnings.append(registered.error());
        }
    }
}

// <file> (relative to the pack, e.g. worldgen/terrain.json): one settings object of the pack,
// parsed; std::nullopt (with a warning when it is broken) when there is none. Neither the file nor
// its folder may be a symbolic link.
template<typename Parse>
auto PackReader::readSettingsFile(const QString &file, Parse parse)
    -> std::optional<typename std::invoke_result_t<Parse, const QJsonObject &, const QString &>::value_type>
{
    const QString path = m_root.filePath(file);
    const QString folder = QFileInfo(file).path();
    if ((folder != u"."_s && QFileInfo(m_root.filePath(folder)).isSymLink()) || QFileInfo(path).isSymLink() || !QFileInfo::exists(path)) {
        return std::nullopt;
    }

    const QcResult<QJsonObject> json = readJsonObject(path);
    if (!json) {
        m_warnings.append(u"%1: %2"_s.arg(path, json.error()));
        return std::nullopt;
    }

    auto parsed = parse(*json, m_packId);
    if (!parsed) {
        m_warnings.append(u"%1: %2"_s.arg(path, parsed.error()));
        return std::nullopt;
    }
    return std::move(*parsed);
}

void PackReader::loadTextures()
{
    const QStringList textureFiles = filesIn(m_root.filePath(u"textures"_s), u"*.json"_s);
    for (const QString &path : textureFiles) {
        const QcResult<QJsonObject> json = readJsonObject(path);
        const QcResult<QcTextureDefinition> texture =
            json ? QcContentParser::parseProceduralTexture(*json, m_packId) : QcResult<QcTextureDefinition>(qcError(json.error()));
        if (!texture) {
            m_warnings.append(u"%1: %2"_s.arg(path, texture.error()));
            continue;
        }
        // Kept: the frames only cost atlas space (decision E-878).
        const QString pattern = texture->procedural.value(u"pattern"_s).toString();
        const QcResult<QcTextureAnimation> animation = QcTextureAnimation::read(texture->procedural);
        if (animation && animation->animated() && !QcTextureAnimation::patternMoves(pattern)) {
            m_warnings.append(u"%1: texture '%2': pattern '%3' does not move, its %4 'frames' all show the same picture"_s.arg(
                path, texture->name, pattern, QString::number(animation->frames)));
        }

        const QList<QcTextureDefinition> tiles = QcContentParser::textureTiles(*texture);
        for (const QcTextureDefinition &tile : tiles) {
            m_registry.assets().registerTexture(tile);
        }
        m_registry.assets().registerTexture(*texture);
    }

    const QStringList pngFiles = filesIn(m_root.filePath(u"textures"_s), u"*.png"_s);
    for (const QString &path : pngFiles) {
        const QcResult<QByteArray> png = QcContentPackLoader::readPackFile(path);
        if (!png) {
            m_warnings.append(png.error());
            continue;
        }

        const QString name = QcContentNames::qualify(QFileInfo(path).completeBaseName(), m_packId);
        if (name.isEmpty()) {
            m_warnings.append(u"%1: invalid texture file name"_s.arg(path));
            continue;
        }

        m_registry.assets().registerTexture({name, {}, *png});
    }
}

void PackReader::loadShapes()
{
    loadDefinitions(u"shapes"_s, QcContentParser::parseShape,
                    [this](QcShapeDefinition shape) { return m_registry.registerShape(std::move(shape)); });
}

void PackReader::loadBlocks()
{
    const QStringList blockFiles = filesIn(m_root.filePath(u"blocks"_s), u"*.json"_s);
    for (const QString &path : blockFiles) {
        const QcResult<QJsonObject> json = readJsonObject(path);
        if (!json) {
            m_warnings.append(json.error());
            continue;
        }

        QcResult<QcBlockDefinition> block = QcContentParser::parseBlock(*json, m_packId);
        if (!block) {
            m_warnings.append(u"%1: %2"_s.arg(path, block.error()));
            continue;
        }

        const QcResult<QcBlockId> id = m_registry.registerBlock(std::move(*block));
        if (!id) {
            m_warnings.append(u"%1: %2"_s.arg(path, id.error()));
        }
    }
}

// The file among `extensions` after `index` that defines the style `name` (a named extension), or
// an empty string.
QString laterDefinition(const QList<std::pair<QString, QJsonObject>> &extensions, qsizetype index, const QString &name,
                        const QString &packId)
{
    for (qsizetype later = index + 1; later < extensions.size(); ++later) {
        const QJsonValue own = extensions[later].second.value(u"name"_s);
        if (own.isString() && QcContentNames::qualify(own.toString(), packId) == name) {
            return extensions[later].first;
        }
    }
    return {};
}

// Applies the extensions of a pack in file-name order, each over the style as the earlier packs and
// files left it. An extension of an unknown style is left out with a warning (which names the
// later file of the pack that defines it, if one does).
void PackReader::applyStyleExtensions(const QList<std::pair<QString, QJsonObject>> &extensions)
{
    for (qsizetype index = 0; index < extensions.size(); ++index) {
        const auto &[path, json] = extensions[index];
        const QcResult<QString> baseName = QcVillageParser::extendedStyle(json, m_packId);
        const QcVillageStyleDefinition *base = baseName ? m_registry.villageCatalog().style(*baseName) : nullptr;
        if (!baseName) {
            m_warnings.append(u"%1: %2"_s.arg(path, baseName.error()));
            continue;
        }
        if (!base) {
            const QString later = laterDefinition(extensions, index, *baseName, m_packId);
            m_warnings.append(later.isEmpty() ? u"%1: 'extends' names no village style: '%2'"_s.arg(path, json.value(u"extends"_s).toString())
                                              : u"%1: 'extends' names '%2', which the later file %3 defines; rename this file to sort after it"_s
                                                    .arg(path, *baseName, later));
            continue;
        }

        QcResult<QcVillageStyleDefinition> style = QcVillageParser::extendVillageStyle(json, m_packId, *base);
        if (!style) {
            m_warnings.append(u"%1: %2"_s.arg(path, style.error()));
            continue;
        }
        m_registry.villageCatalog().registerStyle(std::move(*style));
    }
}

// The village styles of a pack (decision E-421): first the files without "extends" (each replaces a
// style of the same name whole), then those with it (applyStyleExtensions).
void PackReader::loadVillageStyles()
{
    QList<std::pair<QString, QJsonObject>> extensions;
    const QStringList files = filesIn(m_root.filePath(u"villages"_s), u"*.json"_s);
    for (const QString &path : files) {
        const QcResult<QJsonObject> json = readJsonObject(path);
        if (!json) {
            m_warnings.append(json.error());
        } else if (json->contains(u"extends"_s)) {
            extensions.append({path, *json});
        } else if (QcResult<QcVillageStyleDefinition> style = QcVillageParser::parseVillageStyle(*json, m_packId)) {
            m_registry.villageCatalog().registerStyle(std::move(*style));
        } else {
            m_warnings.append(u"%1: %2"_s.arg(path, style.error()));
        }
    }
    applyStyleExtensions(extensions);
}

// Definition folders loaded after the blocks, in this order.
void PackReader::loadLaterDefinitions()
{
    loadDefinitions(u"items"_s, QcContentParser::parseItem,
                    [this](QcItemDefinition item) -> QcStatus {
                        const QString name = item.name;
                        if (const QcResult<QcItemId> id = m_registry.registerItem(std::move(item)); !id) {
                            return qcError(u"item %1: %2"_s.arg(name, id.error()));
                        }
                        return {};
                    });

    loadDefinitions(u"recipes"_s, QcContentParser::parseRecipe,
                    [this](QcRecipeDefinition recipe) { m_registry.registerRecipe(std::move(recipe)); });
    loadDefinitions(u"smelting"_s, QcContentParser::parseSmeltingRecipe,
                    [this](QcSmeltingRecipe recipe) { m_registry.registerSmeltingRecipe(std::move(recipe)); });
    loadDefinitions(u"stonecutting"_s, QcContentParser::parseStonecuttingRecipe,
                    [this](QcSmeltingRecipe recipe) { m_registry.registerStonecuttingRecipe(std::move(recipe)); });
    loadDefinitions(u"parts"_s, QcContentParser::parsePartFamily,
                    [this](QcPartFamily family) { m_registry.registerPartFamily(std::move(family)); });
    loadDefinitions(u"materials"_s, QcContentParser::parseMaterial,
                    [this](QcMaterialDefinition material) { m_registry.registerMaterial(std::move(material)); });
    loadDefinitions(u"mobs"_s, QcContentParser::parseMob,
                    [this](QcMobDefinition mob) { m_registry.registerMob(std::move(mob)); });
    loadDefinitions(u"models"_s, QcContentParser::parseModel,
                    [this](QcModelDefinition model) { m_registry.assets().registerModel(std::move(model)); });
    loadDefinitions(u"appearance"_s, QcContentParser::parseAppearance,
                    [this](QcAppearanceDefinition appearance) { m_registry.assets().registerAppearance(std::move(appearance)); });
    loadDefinitions(u"sounds"_s, QcContentParser::parseSound,
                    [this](QcSoundDefinition sound) { m_registry.assets().registerSound(std::move(sound)); });
    loadDefinitions(u"biomes"_s, QcContentParser::parseBiome,
                    [this](QcBiomeDefinition biome) { m_registry.worldgen().registerBiome(std::move(biome)); });
    loadDefinitions(u"features"_s, QcContentParser::parseFeature,
                    [this](QcFeatureDefinition feature) { m_registry.worldgen().registerFeature(std::move(feature)); });
    loadDefinitions(u"ores"_s, QcContentParser::parseOre,
                    [this](QcOreDefinition ore) { m_registry.worldgen().registerOre(std::move(ore)); });
    loadDefinitions(u"handbook"_s, QcContentParser::parseHandbookChapter,
                    [this](QcHandbookChapter chapter) { m_registry.registerHandbookChapter(std::move(chapter)); });
    loadDefinitions(u"structures"_s, QcContentParser::parseStructure,
                    [this](QcStructureDefinition structure) { m_registry.worldgen().registerStructure(std::move(structure)); });
    loadVillageStyles();
    loadDefinitions(u"professions"_s, QcContentParser::parseProfession,
                    [this](QcVillagerProfession profession) { m_registry.villageCatalog().registerProfession(std::move(profession)); });
    loadDefinitions(u"villagers"_s, QcContentParser::parseVillagerSettings,
                    [this](QcVillagerSettings settings) { m_registry.villageCatalog().registerVillagerSettings(std::move(settings)); });
    loadDefinitions(u"trades"_s, QcContentParser::parseTradeSet,
                    [this](QcTradeSet set) { m_registry.villageCatalog().registerTradeSet(std::move(set)); });
    loadDefinitions(u"projectiles"_s, QcContentParser::parseProjectile,
                    [this](QcProjectileDefinition projectile) { m_registry.registerProjectile(std::move(projectile)); });
    loadDefinitions(u"effects"_s, QcContentParser::parseEffect,
                    [this](QcEffectDefinition effect) { m_registry.registerEffect(std::move(effect)); });
    loadDefinitions(u"enchantments"_s, QcContentParser::parseEnchantment,
                    [this](QcEnchantmentDefinition enchantment) { m_registry.registerEnchantment(std::move(enchantment)); });
    loadDefinitions(u"loot"_s, QcContentParser::parseLootTable,
                    [this](QcLootTableDefinition table) { m_registry.lootTables().add(std::move(table)); });
    loadDefinitions(u"commands"_s, QcContentParser::parseCommandMacro,
                    [this](QcCommandMacroDefinition macro) { m_registry.registerCommandMacro(std::move(macro)); });
}

void PackReader::loadWorldgenSettings()
{
    if (auto terrain = readSettingsFile(u"worldgen/terrain.json"_s, QcContentParser::parseTerrainSettings)) {
        m_registry.worldgen().setTerrainSettings(std::move(*terrain));
    }
    if (auto showcase = readSettingsFile(u"worldgen/showcase.json"_s, QcContentParser::parseShowcase)) {
        m_registry.worldgen().setShowcase(std::move(*showcase));
    }
    if (auto flat = readSettingsFile(u"worldgen/flat.json"_s, QcContentParser::parseFlat)) {
        m_registry.worldgen().setFlatWorld(std::move(*flat));
    }
    // Read over the earlier packs' settings: a pack changes the fields it gives (decision E-421).
    const auto villageSettings = [this](const QJsonObject &json, const QString &pack) {
        return QcVillageParser::parseVillageSettings(json, pack, m_registry.villageCatalog().settings());
    };
    if (auto villages = readSettingsFile(u"worldgen/villages.json"_s, villageSettings)) {
        m_registry.villageCatalog().setSettings(std::move(*villages));
    }
    if (auto empty = readSettingsFile(u"worldgen/void.json"_s, QcContentParser::parseVoid)) {
        m_registry.worldgen().setVoidWorld(std::move(*empty));
    }
}

// Why the block `name` cannot be the snow layer, or an empty string when it can: it must be a block
// of this or an earlier pack that is not opaque, no liquid and needs support below (QcSnowfall
// puts it on the top of a column, decision E-397).
QString unfitSnowLayer(const QcContentRegistry &registry, const QString &name)
{
    const std::optional<QcBlockId> id = registry.blockId(name);
    if (!id) {
        return u"snow layer %1 is not a block"_s.arg(name);
    }
    const QcBlockDefinition &block = registry.block(*id);
    if (block.opaque || block.liquid || block.support != QcBlockSupport::Below) {
        return u"snow layer %1 must be a block that is not opaque, no liquid and needs support below"_s.arg(name);
    }
    return {};
}

void PackReader::loadWeatherSettings()
{
    if (QFileInfo::exists(m_root.filePath(u"villagers.json"_s))) {
        m_warnings.append(u"%1: moved to villagers/<name>.json and professions/<name>.json; the file is left out"_s.arg(m_root.filePath(u"villagers.json"_s)));
    }
    auto weather = readSettingsFile(u"weather.json"_s, QcContentParser::parseWeather);
    if (!weather) {
        return;
    }
    if (const QString unfit = unfitSnowLayer(m_registry, weather->snow.layer); !unfit.isEmpty()) {
        m_warnings.append(u"%1: %2"_s.arg(m_root.filePath(u"weather.json"_s), unfit));
        return;
    }
    m_registry.setWeather(std::move(*weather));
}

void PackReader::loadHoldDefaults()
{
    if (auto defaults = readSettingsFile(u"held.json"_s, QcContentParser::parseHoldDefaults)) {
        m_registry.mergeHoldDefaults(*defaults);
    }
}

// Whether the new keys of a language file keep all languages within QcStreamLimits::maxTranslations
// (decision E-371): checked per file, so a mod never builds a table that clients would refuse.
[[nodiscard]] bool fitsTranslationBudget(const QcContentRegistry &registry, const QString &language, const QHash<QString, QString> &entries)
{
    qsizetype added = 0;
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
        added += registry.translations().has(it.key(), language) ? 0 : 1;
    }
    return registry.translations().total() + added <= static_cast<qsizetype>(QcStreamLimits::maxTranslations);
}

void PackReader::loadTranslations()
{
    const QStringList languageFiles = filesIn(m_root.filePath(u"lang"_s), u"*.json"_s);
    for (const QString &path : languageFiles) {
        const QcResult<QJsonObject> json = readJsonObject(path);
        const QcResult<QHash<QString, QString>> entries =
            json ? QcContentParser::parseTranslations(*json) : QcResult<QHash<QString, QString>>(qcError(json.error()));
        if (!entries) {
            m_warnings.append(u"%1: %2"_s.arg(path, entries.error()));
            continue;
        }

        // "de.json" and "de.handbook.json" both hold German texts; other dots belong to the language.
        QString language = QFileInfo(path).completeBaseName();
        const QLatin1StringView suffix(QcContentPackLoader::handbookLanguageSuffix);
        if (language.endsWith(suffix)) {
            language.chop(suffix.size());
        }
        if (fitsTranslationBudget(m_registry, language, *entries)) {
            m_registry.translations().add(language, *entries);
        } else {
            m_warnings.append(u"%1: more than %2 translations in all languages"_s.arg(path).arg(QcStreamLimits::maxTranslations));
        }
    }
}

} // namespace

QcResult<QcLoadedPack> QcContentPackLoader::load(const QString &directory, QcContentRegistry &registry)
{
    QcResult<QcContentPackInfo> info = readManifest(directory);
    if (!info) {
        return qcError(info.error());
    }

    PackReader pack(directory, info->id, registry);
    pack.loadTextures();
    pack.loadShapes();
    pack.loadBlocks();
    pack.loadLaterDefinitions();
    pack.loadWorldgenSettings();
    pack.loadWeatherSettings();
    pack.loadHoldDefaults();
    pack.loadTranslations();

    return QcLoadedPack{std::move(*info), pack.warnings()};
}
