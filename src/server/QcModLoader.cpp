#include "QcModLoader.h"

#include "QcBaseContent.h"
#include "QcProtocol.h"
#include "QcSoundKeys.h"
#include "QcStreamLimits.h"
#include "QcWorldLibrary.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

constexpr qint64 maxSettingsBytes = qint64(64) * 1024;

// An item name, or "#tag" carried by at least one item.
bool knowsItem(const QcContentRegistry &content, const QString &name)
{
    if (name.startsWith(u'#')) {
        return std::ranges::any_of(content.items(), [tag = name.mid(1)](const QcItemDefinition &definition) {
            return definition.tags.contains(tag);
        });
    }
    return content.itemId(name).has_value();
}

bool knowsTexture(const QcContentRegistry &content, const QString &name)
{
    return name.isEmpty() || content.texture(name) != nullptr;
}

bool knowsSound(const QcContentRegistry &content, const QString &name)
{
    return name.isEmpty() || content.sound(name) != nullptr;
}

void checkRecipeReferences(const QcContentRegistry &content, QStringList &warnings)
{
    for (const QcRecipeDefinition &recipe : content.recipes()) {
        QStringList names = recipe.ingredients;
        for (const auto &[key, name] : recipe.keys) {
            names.append(name);
        }
        names.append(recipe.resultItem);

        for (const QString &name : std::as_const(names)) {
            if (!knowsItem(content, name)) {
                warnings.append(u"recipe %1: unknown item '%2'"_s.arg(recipe.name, name));
            }
        }
    }

    for (const QcSmeltingRecipe &recipe : content.smeltingRecipes()) {
        for (const QString &name : {recipe.inputItem, recipe.resultItem}) {
            if (!knowsItem(content, name)) {
                warnings.append(u"smelting recipe %1: unknown item '%2'"_s.arg(recipe.name, name));
            }
        }

        // Only the first recipe per category and input counts; a later one is never used.
        const QcSmeltingRecipe *used = content.smeltingRecipe(recipe.category, recipe.inputItem);
        if (used && used->name != recipe.name) {
            warnings.append(u"smelting recipe %1 is never used: %2 already handles '%3' in category %4"_s.arg(
                recipe.name, used->name, recipe.inputItem, recipe.category));
        }
    }
}

void checkMobReferences(const QcContentRegistry &content, const QcMobDefinition &mob, QStringList &warnings)
{
    for (const QcItemDrop &drop : mob.drops) {
        if (!knowsItem(content, drop.item)) {
            warnings.append(u"mob %1: unknown drop '%2'"_s.arg(mob.name, drop.item));
        }
    }

    for (const QcMobPart &part : mob.parts) {
        if (!knowsTexture(content, part.texture)) {
            warnings.append(u"mob %1: unknown texture '%2'"_s.arg(mob.name, part.texture));
        }
    }

    if (!mob.skeleton.isEmpty() && !content.model(mob.skeleton)) {
        warnings.append(u"mob %1: unknown skeleton '%2'"_s.arg(mob.name, mob.skeleton));
    }

    const QcAppearanceDefinition *outfit = mob.outfit.isEmpty() ? nullptr : content.appearance(mob.outfit);
    if (!mob.outfit.isEmpty() && (!outfit || outfit->kind != QcAppearanceDefinition::Kind::Outfit)) {
        warnings.append(u"mob %1: unknown outfit '%2'"_s.arg(mob.name, mob.outfit));
    }

    for (const QString &name : {mob.ambientSound, mob.hurtSound, mob.deathSound}) {
        if (!knowsSound(content, name)) {
            warnings.append(u"mob %1: unknown sound '%2'"_s.arg(mob.name, name));
        }
    }
}

void checkBlockReferences(const QcContentRegistry &content, const QcBlockDefinition &block, QStringList &warnings)
{
    for (const QcItemDrop &drop : block.drops) {
        if (!knowsItem(content, drop.item)) {
            warnings.append(u"block %1: unknown drop '%2'"_s.arg(block.name, drop.item));
        }
    }

    // Silent at run time; the author should know.
    for (int action = 0; action < qcBlockSoundActionCount; ++action) {
        const QString name = QcSoundKeys::blockSound(block, QcBlockSoundAction(action));
        if (!knowsSound(content, name)) {
            warnings.append(u"block %1: unknown sound '%2'"_s.arg(block.name, name));
        }
    }

    if (!knowsSound(content, block.machine.sound)) {
        warnings.append(u"block %1: unknown machine sound '%2'"_s.arg(block.name, block.machine.sound));
    }
}

// Names a definition refers to that no pack provides; such entries stay inert (a recipe never
// matches, a drop is not given) but deserve a warning for the mod author.
void checkReferences(const QcContentRegistry &content, QStringList &warnings)
{
    checkRecipeReferences(content, warnings);

    for (const QcMobDefinition &mob : content.mobs()) {
        checkMobReferences(content, mob, warnings);
    }

    for (const QcAppearanceDefinition &appearance : content.appearances()) {
        for (const QcAppearanceLayer &layer : appearance.layers) {
            if (!knowsTexture(content, layer.texture)) {
                warnings.append(u"appearance %1: unknown texture '%2'"_s.arg(appearance.name, layer.texture));
            }
        }
    }

    for (const QcBlockDefinition &block : content.blocks()) {
        checkBlockReferences(content, block, warnings);
    }

    for (const QcItemDefinition &definition : content.items()) {
        if (!knowsTexture(content, definition.texture)) {
            warnings.append(u"item %1: unknown texture '%2'"_s.arg(definition.name, definition.texture));
        }
        if (!definition.placesBlock.isEmpty() && !content.blockId(definition.placesBlock)) {
            warnings.append(u"item %1: places unknown block '%2'"_s.arg(definition.name, definition.placesBlock));
        }
    }
}

// Room the content sync frame leaves for the registry.
constexpr qint64 contentSyncBudget = QcProtocol::maxFrameBytes - 4096;
// Bytes all mods together may make the loader read (hard links count every time they are read).
constexpr qint64 totalReadBudget = qint64(64) * 1024 * 1024;
// World generation cost: ore definitions and their veins x size per chunk, biomes, features.
constexpr qsizetype maxOres = 256;
constexpr qint64 maxOreBlocksPerChunk = 4096;
constexpr qsizetype maxBiomes = 256;
constexpr qsizetype maxFeatures = 1024;
constexpr qsizetype maxPlacements = 4096;
constexpr qint64 maxPoolAttemptsPerChunk = 64;

// Server-side costs that clients never see.
QString worldgenProblem(const QcContentRegistry &content)
{
    qint64 oreBlocks = 0;
    for (const QcOreDefinition &ore : content.ores()) {
        oreBlocks += qint64(ore.veinsPerChunk) * ore.veinSize;
    }
    if (content.ores().size() > maxOres || oreBlocks > maxOreBlocksPerChunk) {
        return u"too many ores (at most %1 definitions and %2 ore blocks per chunk)"_s.arg(maxOres).arg(maxOreBlocksPerChunk);
    }

    qsizetype placements = 0;
    for (const QcBiomeDefinition &biome : content.biomes()) {
        placements += biome.features.size();
    }
    if (content.biomes().size() > maxBiomes || content.features().size() > maxFeatures || placements > maxPlacements) {
        return u"too many biomes or features (at most %1 biomes, %2 features, %3 feature placements)"_s.arg(maxBiomes)
            .arg(maxFeatures)
            .arg(maxPlacements);
    }

    qint64 poolAttempts = 0;
    for (const QcFeatureDefinition &feature : content.features()) {
        poolAttempts += feature.type == QcFeatureDefinition::Type::Pool ? feature.attempts : 0;
    }
    if (poolAttempts > maxPoolAttemptsPerChunk) {
        return u"too many pools (at most %1 pool attempts per chunk)"_s.arg(maxPoolAttemptsPerChunk);
    }

    return {};
}

// Encoded size of the registry as the content sync sends it; `buffer` is reused between calls.
qint64 encodedSize(const QcContentRegistry &content, QByteArray &buffer)
{
    buffer.resize(0);
    QDataStream stream(&buffer, QIODevice::WriteOnly);
    content.writeTo(stream);
    return buffer.size();
}

// The counts clients accept (QcContentRegistry::readFrom); everything else a client checks is already
// enforced by the parser (recipe shapes, model parts, ranges).
QString countProblem(const QcContentRegistry &content)
{
    const auto tooMany = [](qsizetype count) { return std::cmp_greater(count, QcStreamLimits::maxDefinitions); };
    if (tooMany(content.blockCount()) || tooMany(content.itemCount()) || tooMany(content.mobs().size())
        || tooMany(content.recipes().size()) || tooMany(content.smeltingRecipes().size()) || tooMany(content.textures().size())
        || tooMany(content.languages().size())
        || tooMany(content.models().size()) || tooMany(content.appearances().size())
        || content.packs().size() > 1024) {
        return u"clients would reject the content (more than %1 definitions of one kind)"_s.arg(QcStreamLimits::maxDefinitions);
    }

    const QStringList languages = content.languages();
    for (const QString &language : languages) {
        if (tooMany(content.translationCount(language))) {
            return u"clients would reject the content (more than %1 translations for '%2')"_s.arg(QcStreamLimits::maxDefinitions)
                .arg(language);
        }
    }
    return {};
}

// Clients refuse content beyond their limits (sizes, counts, ranges); the encoded registry is read
// back exactly as a client would.
QString clientProblem(const QcContentRegistry &content, QByteArray &buffer)
{
    if (encodedSize(content, buffer) > contentSyncBudget) {
        return u"the content of all packs together exceeds %1 MiB"_s.arg(QcProtocol::maxFrameBytes / (1024 * 1024));
    }

    QDataStream stream(buffer);
    if (const QcResult<QcContentRegistry> decoded = QcContentRegistry::readFrom(stream); !decoded) {
        return u"clients would reject the content (%1)"_s.arg(decoded.error());
    }
    return {};
}

// Bookkeeping of QcModLoader::resolve().
struct ResolveState
{
    QHash<QString, QVersionNumber> loaded;
    QList<QcModStatus> ordered;
    QList<QcModStatus> others;
    QHash<QString, QString> notLoadedBecause;
    QHash<QString, QcModStatus> pending;
};

// Duplicates, the base game's id and disabled mods go to `others`, the rest waits in `pending`.
void sortCandidates(const QList<QcContentPackInfo> &candidates, const QStringList &disabled, ResolveState &state)
{
    for (const QcContentPackInfo &candidate : candidates) {
        QcModStatus status;
        status.info = candidate;
        if (state.loaded.contains(candidate.id)) {
            status.reason = u"the id '%1' belongs to the base game"_s.arg(candidate.id);
        } else if (state.pending.contains(candidate.id) || state.notLoadedBecause.contains(candidate.id)) {
            status.reason = u"another mod already uses the id '%1'"_s.arg(candidate.id);
        } else if (disabled.contains(candidate.id)) {
            status.state = QcModStatus::State::Disabled;
            state.notLoadedBecause.insert(candidate.id, u"is disabled"_s);
            state.others.append(status);
            continue;
        } else {
            state.pending.insert(candidate.id, status);
            continue;
        }
        // A duplicate fails on its own; the first mod with that id keeps its chance.
        state.others.append(status);
    }
}

void failPending(ResolveState &state, const QString &id, const QString &reason)
{
    QcModStatus status = state.pending.take(id);
    status.reason = reason;
    state.notLoadedBecause.insert(id, u"failed"_s);
    state.others.append(status);
}

// Why `dependency` keeps `info` from loading; empty when it is loaded in a sufficient version or
// still pending (then `waiting` is set).
QString dependencyProblem(const QcContentPackInfo &info, const QString &dependency, const ResolveState &state,
                          bool &waiting)
{
    if (state.loaded.contains(dependency)) {
        const QVersionNumber minimum = info.minimumVersions.value(dependency);
        const QVersionNumber found = state.loaded.value(dependency);
        if (!minimum.isNull() && found < minimum) {
            return u"needs '%1' %2 or newer, found %3"_s.arg(dependency, minimum.toString(), found.toString());
        }
    } else if (state.notLoadedBecause.contains(dependency)) {
        return u"depends on '%1', which %2"_s.arg(dependency, state.notLoadedBecause.value(dependency));
    } else if (!state.pending.contains(dependency)) {
        return u"missing dependency '%1'"_s.arg(dependency);
    } else {
        waiting = true;
    }
    return {};
}

// Loads or fails the first pending mod (by id) that can be decided; false when all of them wait.
bool decideNextPending(ResolveState &state)
{
    QStringList ids = state.pending.keys();
    ids.sort();

    for (const QString &id : std::as_const(ids)) {
        const QcContentPackInfo &info = state.pending[id].info;
        QString problem;
        bool waiting = false;
        for (const QString &dependency : info.dependencies) {
            problem = dependencyProblem(info, dependency, state, waiting);
            if (!problem.isEmpty()) {
                break;
            }
        }

        if (!problem.isEmpty()) {
            failPending(state, id, problem);
            return true;
        }

        if (!waiting) {
            QcModStatus status = state.pending.take(id);
            status.state = QcModStatus::State::Loaded;
            state.loaded.insert(id, QcContentPackLoader::parseVersion(status.info.version));
            state.ordered.append(status);
            return true;
        }
    }
    return false;
}

// What a mod being loaded may still use and what earlier attempts left behind.
struct ModLoadState
{
    // Warnings of the base game alone; a mod reports only the ones it adds.
    QStringList baseWarnings;
    QSet<QString> refused;
    qint64 readBytes = 0;
    int attempts = 0;
    QByteArray buffer;
};

// Server and client limits for the finished registry of a candidate; empty when it fits.
QString finishedContentProblem(const QcContentRegistry &finished, QByteArray &buffer)
{
    QString problem = worldgenProblem(finished);
    if (problem.isEmpty()) {
        problem = countProblem(finished);
    }
    if (problem.isEmpty() && encodedSize(finished, buffer) > contentSyncBudget) {
        problem = u"the content of all packs together exceeds %1 MiB"_s.arg(QcProtocol::maxFrameBytes / (1024 * 1024));
    }
    return problem;
}

// Loads `mod` on top of `accepted` and checks it exactly as it would reach clients (with the items
// created for its blocks). Returns why the mod is refused; `accepted` is only changed on success.
QString admitMod(QcModStatus &mod, QcContentRegistry &accepted, ModLoadState &state)
{
    const auto failedDependency = std::ranges::find_if(mod.info.dependencies, [&state](const QString &id) {
        return state.refused.contains(id);
    });
    if (failedDependency != mod.info.dependencies.end()) {
        return u"depends on '%1', which failed"_s.arg(*failedDependency);
    }

    // Every mod that gets this far costs a load and a full check, accepted or not.
    if (state.attempts >= QcModLoader::maxLoadedMods) {
        return u"at most %1 mods are loaded"_s.arg(QcModLoader::maxLoadedMods);
    }

    ++state.attempts;

    // Bytes on disk (hard links count each time) are checked before anything is read; every
    // attempt counts, also for mods that are refused afterwards.
    const qint64 size = QcContentPackLoader::measure(mod.info.directory);
    if (state.readBytes + size > totalReadBudget || size > contentSyncBudget) {
        return u"the mod's files exceed the %1 MiB left for mods"_s.arg((totalReadBudget - state.readBytes) / (qint64(1024) * 1024));
    }

    state.readBytes += size;

    QcContentRegistry candidate = accepted;
    const QcResult<QcContentPackInfo> info = QcContentPackLoader::load(mod.info.directory, candidate, mod.warnings);
    if (!info) {
        return info.error();
    }

    candidate.addPack({info->id, info->name, info->version});

    QcContentRegistry finished = candidate;
    const QStringList partWarnings = finished.finish();
    for (const QString &warning : partWarnings) {
        if (!state.baseWarnings.contains(warning)) {
            mod.warnings.append(warning);
        }
    }

    if (QString problem = finishedContentProblem(finished, state.buffer); !problem.isEmpty()) {
        return problem;
    }

    accepted = std::move(candidate);
    return {};
}

// Safety net: read the final content back exactly like a client. The checks of admitMod() should
// make this pass; if not, the game starts with the base content alone rather than with content
// clients refuse.
void fallBackToBaseIfRefused(QcModLoader::Result &result, QcContentRegistry &base, QByteArray &buffer)
{
    const QString problem = clientProblem(*result.content, buffer);
    if (problem.isEmpty()) {
        return;
    }

    qWarning("Mods: clients would reject the combined content (%s); starting without mods", qPrintable(problem));
    for (QcModStatus &mod : result.mods) {
        if (mod.state == QcModStatus::State::Loaded) {
            mod.state = QcModStatus::State::Failed;
            mod.reason = u"clients would reject the combined content of all mods (%1)"_s.arg(problem);
        }
    }

    result.content = std::make_shared<QcContentRegistry>(std::move(base));
    result.content->finish();
}

void reportModWarnings(const QList<QcModStatus> &mods, QStringList &warnings)
{
    for (const QcModStatus &mod : mods) {
        const QString name = mod.info.id.isEmpty() ? mod.info.directory : mod.info.id;
        if (mod.state != QcModStatus::State::Loaded) {
            warnings.append(u"mod %1 not loaded: %2"_s.arg(name, mod.state == QcModStatus::State::Disabled ? u"disabled"_s : mod.reason));
            continue;
        }

        for (const QString &warning : mod.warnings) {
            warnings.append(u"mod %1: %2"_s.arg(name, warning));
        }
    }
}

} // namespace

QString QcModLoader::defaultDirectory()
{
    return QDir(QcWorldLibrary::dataRoot()).filePath(u"mods"_s);
}

QStringList QcModLoader::disabledMods(const QString &modsDirectory)
{
    if (modsDirectory.isEmpty()) {
        return {};
    }

    const QcResult<QByteArray> data = QcContentPackLoader::readPackFile(QDir(modsDirectory).filePath(QString::fromLatin1(settingsFile)));
    if (!data || data->size() > maxSettingsBytes) {
        return {};
    }

    QStringList disabled;
    const QJsonArray list = QJsonDocument::fromJson(*data).object().value(u"disabled"_s).toArray();
    for (const auto &entry : list) {
        if (entry.isString() && !disabled.contains(entry.toString())) {
            disabled.append(entry.toString());
        }
    }
    return disabled;
}

QcStatus QcModLoader::setModEnabled(const QString &modsDirectory, const QString &id, bool enabled)
{
    if (modsDirectory.isEmpty() || !QDir().mkpath(modsDirectory)) {
        return qcError(u"Cannot create the mods directory"_s);
    }

    QStringList disabled = disabledMods(modsDirectory);
    disabled.removeAll(id);
    if (!enabled) {
        disabled.append(id);
    }
    disabled.sort();

    QSaveFile file(QDir(modsDirectory).filePath(QString::fromLatin1(settingsFile)));
    if (!file.open(QIODevice::WriteOnly)) {
        return qcError(file.errorString());
    }

    file.write(QJsonDocument(QJsonObject{{u"disabled"_s, QJsonArray::fromStringList(disabled)}}).toJson());
    if (!file.commit()) {
        return qcError(file.errorString());
    }

    return {};
}

QList<QcModStatus> QcModLoader::resolve(const QList<QcContentPackInfo> &candidates, const QStringList &disabled,
                                        const QList<QcPackReference> &available)
{
    ResolveState state;
    for (const QcPackReference &pack : available) {
        state.loaded.insert(pack.id, QcContentPackLoader::parseVersion(pack.version));
    }

    sortCandidates(candidates, disabled, state);

    while (!state.pending.isEmpty()) {
        if (!decideNextPending(state)) {
            // Everything left waits on each other.
            QStringList ids = state.pending.keys();
            ids.sort();
            for (const QString &id : std::as_const(ids)) {
                failPending(state, id, u"dependency cycle among: %1"_s.arg(ids.join(u", "_s)));
            }
        }
    }

    std::ranges::sort(state.others, {}, [](const QcModStatus &status) { return status.info.directory; });
    return state.ordered + state.others;
}

QList<QcModStatus> QcModLoader::discover(const QString &modsDirectory)
{
    if (modsDirectory.isEmpty()) {
        return {};
    }

    const QDir root(modsDirectory);
    const QStringList folders = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    QList<QcContentPackInfo> candidates;
    QList<QcModStatus> broken;
    const QStringList considered = folders.first(std::min<qsizetype>(folders.size(), maxMods));
    for (const QString &folder : considered) {
        const QString directory = root.filePath(folder);
        if (QFileInfo(directory).isSymLink()) {
            // A linked pack could pull in files from anywhere; packs must be real directories.
            QcModStatus status;
            status.info.directory = directory;
            status.reason = u"the mod folder is a symbolic link"_s;
            broken.append(status);
            continue;
        }

        QcResult<QcContentPackInfo> info = QcContentPackLoader::readManifest(directory);
        if (info) {
            candidates.append(std::move(*info));
            continue;
        }

        QcModStatus status;
        status.info.directory = directory;
        status.reason = info.error();
        broken.append(status);
    }

    QList<QcPackReference> base;
    if (const QcResult<QcContentPackInfo> info =
            QcContentPackLoader::readManifest(QString::fromLatin1(QcBaseContent::resourceDirectory))) {
        base.append({info->id, info->name, info->version});
    }

    return resolve(candidates, disabledMods(modsDirectory), base) + broken;
}

QcResult<QcModLoader::Result> QcModLoader::load(const QString &modsDirectory, QStringList &warnings)
{
    Result result;
    QcContentRegistry base;
    if (const QcResult<QcContentPackInfo> loaded = QcBaseContent::loadInto(base, warnings); !loaded) {
        return qcError(loaded.error());
    }

    ModLoadState state;
    {
        QcContentRegistry finished = base;
        state.baseWarnings = finished.finish();
        warnings += state.baseWarnings;
        if (const QString problem = clientProblem(finished, state.buffer); !problem.isEmpty()) {
            return qcError(u"base game: "_s + problem);
        }
    }

    result.mods = discover(modsDirectory);

    // Each mod is checked before the next one loads; a refused mod leaves no trace. Reads, mods and
    // checks are bounded, so the time and memory of a start are too.
    QcContentRegistry accepted = base;
    for (QcModStatus &mod : result.mods) {
        if (mod.state != QcModStatus::State::Loaded) {
            continue;
        }

        if (const QString reason = admitMod(mod, accepted, state); !reason.isEmpty()) {
            mod.state = QcModStatus::State::Failed;
            mod.reason = reason;
            state.refused.insert(mod.info.id);
        }
    }

    result.content = std::make_shared<QcContentRegistry>(std::move(accepted));
    // Warnings of the parts were reported with the mod that caused them.
    result.content->finish();
    fallBackToBaseIfRefused(result, base, state.buffer);
    reportModWarnings(result.mods, warnings);
    checkReferences(*result.content, warnings);
    return result;
}
