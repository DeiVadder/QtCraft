#include "worlds/QcModLoader.h"

#include "common/content/loader/QcBaseContent.h"
#include "common/protocol/QcProtocol.h"
#include "common/protocol/QcStreamLimits.h"
#include "server/core/QcServerLogging.h"
#include "worlds/QcContentReferences.h"
#include "worlds/QcWorldLibrary.h"

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

constexpr qint64 maxSettingsBytes = qint64{64} * 1024;

// Room the content sync frame leaves for the registry.
constexpr qint64 contentSyncBudget = QcProtocol::maxFrameBytes - 4096;
// Bytes all mods together may make the loader read (hard links count every time they are read).
constexpr qint64 totalReadBudget = qint64{64} * 1024 * 1024;
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
    for (const QcOreDefinition &ore : content.worldgen().ores()) {
        oreBlocks += static_cast<qint64>(ore.veinsPerChunk) * ore.veinSize;
    }
    if (content.worldgen().ores().size() > maxOres || oreBlocks > maxOreBlocksPerChunk) {
        return u"too many ores (at most %1 definitions and %2 ore blocks per chunk)"_s.arg(maxOres).arg(maxOreBlocksPerChunk);
    }

    qsizetype placements = 0;
    for (const QcBiomeDefinition &biome : content.worldgen().biomes()) {
        placements += biome.features.size();
    }
    if (content.worldgen().biomes().size() > maxBiomes || content.worldgen().features().size() > maxFeatures || placements > maxPlacements) {
        return u"too many biomes or features (at most %1 biomes, %2 features, %3 feature placements)"_s.arg(maxBiomes)
            .arg(maxFeatures)
            .arg(maxPlacements);
    }

    qint64 poolAttempts = 0;
    for (const QcFeatureDefinition &feature : content.worldgen().features()) {
        // A surface lake tries its sites once per square of region × region chunks (decision E-856).
        const qint64 chunks = feature.surface ? static_cast<qint64>(feature.region) * feature.region : 1;
        poolAttempts += feature.type == QcFeatureDefinition::Type::Pool ? (feature.attempts + chunks - 1) / chunks : 0;
    }
    if (poolAttempts > maxPoolAttemptsPerChunk) {
        return u"too many pools (at most %1 pool attempts per chunk)"_s.arg(maxPoolAttemptsPerChunk);
    }

    return {};
}

// The counts clients accept (QcContentRegistry::readFrom); everything else a client checks is already
// enforced by the parser (recipe shapes, model parts, ranges).
QString countProblem(const QcContentRegistry &content)
{
    const auto tooMany = [](qsizetype count) { return std::cmp_greater(count, QcStreamLimits::maxDefinitions); };
    if (tooMany(content.blockCount()) || tooMany(content.itemCount()) || tooMany(content.mobs().size())
        || tooMany(content.recipes().size()) || tooMany(content.smeltingRecipes().size()) || tooMany(content.assets().textures().size())
        || tooMany(content.translations().languages().size())
        || tooMany(content.assets().models().size()) || tooMany(content.assets().appearances().size())
        || content.packs().size() > 1024) {
        return u"clients would reject the content (more than %1 definitions of one kind)"_s.arg(QcStreamLimits::maxDefinitions);
    }

    if (std::cmp_greater(content.translations().total(), QcStreamLimits::maxTranslations)) {
        return u"clients would reject the content (more than %1 translations in all languages)"_s.arg(QcStreamLimits::maxTranslations);
    }

    const QStringList languages = content.translations().languages();
    for (const QString &language : languages) {
        if (tooMany(content.translations().count(language))) {
            return u"clients would reject the content (more than %1 translations for '%2')"_s.arg(QcStreamLimits::maxDefinitions)
                .arg(language);
        }
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

    // Duplicates, the base game's id and disabled mods go to `others`, the rest waits in `pending`.
    void sortCandidates(const QList<QcContentPackInfo> &candidates, const QStringList &disabled);
    void failPending(const QString &id, const QString &reason);
    // Loads or fails the first pending mod (by id) that can be decided; false when all of them wait.
    [[nodiscard]] bool decideNextPending();
};

void ResolveState::sortCandidates(const QList<QcContentPackInfo> &candidates, const QStringList &disabled)
{
    for (const QcContentPackInfo &candidate : candidates) {
        QcModStatus status;
        status.info = candidate;
        if (loaded.contains(candidate.id)) {
            status.reason = u"the id '%1' belongs to the base game"_s.arg(candidate.id);
        } else if (pending.contains(candidate.id) || notLoadedBecause.contains(candidate.id)) {
            status.reason = u"another mod already uses the id '%1'"_s.arg(candidate.id);
        } else if (disabled.contains(candidate.id)) {
            status.state = QcModStatus::State::Disabled;
            notLoadedBecause.insert(candidate.id, u"is disabled"_s);
            others.append(status);
            continue;
        } else {
            pending.insert(candidate.id, status);
            continue;
        }
        // A duplicate fails on its own; the first mod with that id keeps its chance.
        others.append(status);
    }
}

void ResolveState::failPending(const QString &id, const QString &reason)
{
    QcModStatus status = pending.take(id);
    status.reason = reason;
    notLoadedBecause.insert(id, u"failed"_s);
    others.append(status);
}

// What one dependency means for a pending mod.
struct DependencyCheck
{
    // Why the dependency keeps the mod from loading; empty when it does not.
    QString problem;
    // The dependency is still pending itself.
    bool waiting = false;
};

// Whether `dependency` keeps `info` from loading; fine when it is loaded in a sufficient version,
// waiting while it is still pending.
[[nodiscard]] DependencyCheck checkDependency(const QcContentPackInfo &info, const QString &dependency, const ResolveState &state)
{
    if (state.loaded.contains(dependency)) {
        const QVersionNumber minimum = info.minimumVersions.value(dependency);
        const QVersionNumber found = state.loaded.value(dependency);
        if (!minimum.isNull() && found < minimum) {
            return {u"needs '%1' %2 or newer, found %3"_s.arg(dependency, minimum.toString(), found.toString())};
        }
    } else if (state.notLoadedBecause.contains(dependency)) {
        return {u"depends on '%1', which %2"_s.arg(dependency, state.notLoadedBecause.value(dependency))};
    } else if (!state.pending.contains(dependency)) {
        return {u"missing dependency '%1'"_s.arg(dependency)};
    } else {
        return {{}, true};
    }
    return {};
}

bool ResolveState::decideNextPending()
{
    QStringList ids = pending.keys();
    ids.sort();

    for (const QString &id : std::as_const(ids)) {
        const QcContentPackInfo &info = pending[id].info;
        QString problem;
        bool waiting = false;
        for (const QString &dependency : info.dependencies) {
            const DependencyCheck check = checkDependency(info, dependency, *this);
            problem = check.problem;
            waiting = waiting || check.waiting;
            if (!problem.isEmpty()) {
                break;
            }
        }

        if (!problem.isEmpty()) {
            failPending(id, problem);
            return true;
        }

        if (!waiting) {
            QcModStatus status = pending.take(id);
            status.state = QcModStatus::State::Loaded;
            loaded.insert(id, QcContentPackLoader::parseVersion(status.info.version));
            ordered.append(status);
            return true;
        }
    }
    return false;
}

// What a mod being loaded may still use and what earlier attempts left behind.
class ModLoadState
{
public:
    // Warnings of the base game alone; a mod reports only the ones it adds.
    QStringList baseWarnings;
    QSet<QString> refused;
    qint64 readBytes = 0;
    int attempts = 0;

    // Clients refuse content beyond their limits (sizes, counts, ranges); the encoded registry is
    // read back exactly as a client would. Empty when they accept it.
    [[nodiscard]] QString clientProblem(const QcContentRegistry &content);
    // Server and client limits for the finished registry of a candidate; empty when it fits.
    [[nodiscard]] QString finishedContentProblem(const QcContentRegistry &finished);

    // What admitting a mod gave: the accepted content with the mod, or why it is refused, and the
    // warnings of loading it (also of a refused mod).
    struct Admission
    {
        QcResult<QcContentRegistry> content;
        QStringList warnings;
    };

    // Loads `mod` on top of `accepted` and checks it exactly as it would reach clients (with the
    // items created for its blocks).
    [[nodiscard]] Admission admit(const QcModStatus &mod, const QcContentRegistry &accepted);

private:
    // Encoded size of the registry as the content sync sends it, encoded into `m_buffer`.
    [[nodiscard]] qint64 encodedSize(const QcContentRegistry &content);

    // Reused by every encoding of one start.
    QByteArray m_buffer;
};

qint64 ModLoadState::encodedSize(const QcContentRegistry &content)
{
    m_buffer.resize(0);
    QDataStream stream(&m_buffer, QIODevice::WriteOnly);
    content.writeTo(stream);
    return m_buffer.size();
}

QString ModLoadState::clientProblem(const QcContentRegistry &content)
{
    if (encodedSize(content) > contentSyncBudget) {
        return u"the content of all packs together exceeds %1 MiB"_s.arg(QcProtocol::maxFrameBytes / (1024 * 1024));
    }

    QDataStream stream(m_buffer);
    if (const QcResult<QcContentRegistry> decoded = QcContentRegistry::readFrom(stream); !decoded) {
        return u"clients would reject the content (%1)"_s.arg(decoded.error());
    }
    return {};
}

QString ModLoadState::finishedContentProblem(const QcContentRegistry &finished)
{
    QString problem = worldgenProblem(finished);
    if (problem.isEmpty()) {
        problem = countProblem(finished);
    }
    if (problem.isEmpty() && encodedSize(finished) > contentSyncBudget) {
        problem = u"the content of all packs together exceeds %1 MiB"_s.arg(QcProtocol::maxFrameBytes / (1024 * 1024));
    }
    return problem;
}

ModLoadState::Admission ModLoadState::admit(const QcModStatus &mod, const QcContentRegistry &accepted)
{
    const auto failedDependency = std::ranges::find_if(mod.info.dependencies, [this](const QString &id) { return refused.contains(id); });
    if (failedDependency != mod.info.dependencies.end()) {
        return {qcError(u"depends on '%1', which failed"_s.arg(*failedDependency)), {}};
    }

    // Every mod that gets this far costs a load and a full check, accepted or not.
    if (attempts >= QcModLoader::maxLoadedMods) {
        return {qcError(u"at most %1 mods are loaded"_s.arg(QcModLoader::maxLoadedMods)), {}};
    }

    ++attempts;

    // Bytes on disk (hard links count each time) are checked before anything is read; every
    // attempt counts, also for mods that are refused afterwards.
    const qint64 size = QcContentPackLoader::measure(mod.info.directory);
    if (readBytes + size > totalReadBudget || size > contentSyncBudget) {
        return {qcError(u"the mod's files exceed the %1 MiB left for mods"_s.arg((totalReadBudget - readBytes) / (qint64{1024} * 1024))), {}};
    }

    readBytes += size;

    QcContentRegistry candidate = accepted;
    const QcResult<QcLoadedPack> loaded = QcContentPackLoader::load(mod.info.directory, candidate);
    if (!loaded) {
        return {qcError(loaded.error()), {}};
    }

    Admission admission{QcContentRegistry(), loaded->warnings};
    candidate.addPack({loaded->info.id, loaded->info.name, loaded->info.version});

    QcContentRegistry finished = candidate;
    const QStringList partWarnings = finished.finish();
    for (const QString &warning : partWarnings) {
        if (!baseWarnings.contains(warning)) {
            admission.warnings.append(warning);
        }
    }

    if (const QString problem = finishedContentProblem(finished); !problem.isEmpty()) {
        admission.content = qcError(problem);
        return admission;
    }

    admission.content = std::move(candidate);
    return admission;
}

// The result with the base content alone, for combined content that clients would reject
// (`problem`): every loaded mod counts as failed.
[[nodiscard]] QcModLoader::Result withBaseOnly(QcModLoader::Result result, QcContentRegistry base, const QString &problem)
{
    qCWarning(lcContent, "Mods: clients would reject the combined content (%s); starting without mods", qPrintable(problem));
    for (QcModStatus &mod : result.mods) {
        if (mod.state == QcModStatus::State::Loaded) {
            mod.state = QcModStatus::State::Failed;
            mod.reason = u"clients would reject the combined content of all mods (%1)"_s.arg(problem);
        }
    }

    result.content = std::make_shared<QcContentRegistry>(std::move(base));
    result.content->finish();
    return result;
}

[[nodiscard]] QStringList modWarnings(const QList<QcModStatus> &mods)
{
    QStringList warnings;
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
    return warnings;
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

    state.sortCandidates(candidates, disabled);

    while (!state.pending.isEmpty()) {
        if (!state.decideNextPending()) {
            // Everything left waits on each other.
            QStringList ids = state.pending.keys();
            ids.sort();
            for (const QString &id : std::as_const(ids)) {
                state.failPending(id, u"dependency cycle among: %1"_s.arg(ids.join(u", "_s)));
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

QcModLoader::Loaded QcModLoader::load(const QString &modsDirectory)
{
    Result result;
    QcContentRegistry base;
    // The pack loader fails only on an unusable manifest, before it reads (and warns about) anything.
    const QcResult<QcLoadedPack> loaded = QcBaseContent::loadInto(base);
    if (!loaded) {
        return std::unexpected(Failure{loaded.error(), {}});
    }
    result.warnings = loaded->warnings;

    ModLoadState state;
    {
        QcContentRegistry finished = base;
        state.baseWarnings = finished.finish();
        result.warnings += state.baseWarnings;
        if (const QString problem = state.clientProblem(finished); !problem.isEmpty()) {
            return std::unexpected(Failure{u"base game: "_s + problem, std::move(result.warnings)});
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

        ModLoadState::Admission admission = state.admit(mod, accepted);
        mod.warnings += admission.warnings;
        if (!admission.content) {
            mod.state = QcModStatus::State::Failed;
            mod.reason = admission.content.error();
            state.refused.insert(mod.info.id);
            continue;
        }
        accepted = std::move(*admission.content);
    }

    result.content = std::make_shared<QcContentRegistry>(std::move(accepted));
    // Warnings of the parts were reported with the mod that caused them.
    result.content->finish();
    // Safety net: read the final content back exactly like a client. The checks of ModLoadState::admit() should
    // make this pass; if not, the game starts with the base content alone rather than with content
    // clients refuse.
    if (const QString problem = state.clientProblem(*result.content); !problem.isEmpty()) {
        result = withBaseOnly(std::move(result), std::move(base), problem);
    }
    result.warnings += modWarnings(result.mods);
    result.warnings += QcContentReferences::check(*result.content);
    return result;
}
