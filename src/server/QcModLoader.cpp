#include "QcModLoader.h"

#include "QcBaseContent.h"
#include "QcProtocol.h"
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

using namespace Qt::StringLiterals;

namespace {

constexpr qint64 maxSettingsBytes = qint64(64) * 1024;

// Names a definition refers to that no pack provides; such entries stay inert (a recipe never
// matches, a drop is not given) but deserve a warning for the mod author.
void checkReferences(const QcContentRegistry &content, QStringList &warnings)
{
    const auto item = [&content](const QString &name) { return content.itemId(name).has_value(); };
    const auto texture = [&content](const QString &name) { return name.isEmpty() || content.texture(name) != nullptr; };
    for (const QcRecipeDefinition &recipe : content.recipes()) {
        QStringList names = recipe.ingredients;
        for (const auto &[key, name] : recipe.keys) {
            names.append(name);
        }
        names.append(recipe.resultItem);
        for (const QString &name : std::as_const(names)) {
            if (!item(name)) {
                warnings.append(u"recipe %1: unknown item '%2'"_s.arg(recipe.name, name));
            }
        }
    }
    for (const QcMobDefinition &mob : content.mobs()) {
        for (const QcItemDrop &drop : mob.drops) {
            if (!item(drop.item)) {
                warnings.append(u"mob %1: unknown drop '%2'"_s.arg(mob.name, drop.item));
            }
        }
        for (const QcMobPart &part : mob.parts) {
            if (!texture(part.texture)) {
                warnings.append(u"mob %1: unknown texture '%2'"_s.arg(mob.name, part.texture));
            }
        }
    }
    for (const QcBlockDefinition &block : content.blocks()) {
        for (const QcItemDrop &drop : block.drops) {
            if (!item(drop.item)) {
                warnings.append(u"block %1: unknown drop '%2'"_s.arg(block.name, drop.item));
            }
        }
    }
    for (const QcItemDefinition &definition : content.items()) {
        if (!texture(definition.texture)) {
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
    if (content.biomes().size() > maxBiomes || content.features().size() > maxFeatures) {
        return u"too many biomes or features (at most %1 and %2)"_s.arg(maxBiomes).arg(maxFeatures);
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
    QHash<QString, QVersionNumber> loaded;
    for (const QcPackReference &pack : available) {
        loaded.insert(pack.id, QcContentPackLoader::parseVersion(pack.version));
    }

    QList<QcModStatus> ordered;
    QList<QcModStatus> others;
    QHash<QString, QString> notLoadedBecause;
    QHash<QString, QcModStatus> pending;
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

    const auto fail = [&](const QString &id, const QString &reason) {
        QcModStatus status = pending.take(id);
        status.reason = reason;
        notLoadedBecause.insert(id, u"failed"_s);
        others.append(status);
    };
    while (!pending.isEmpty()) {
        QStringList ids = pending.keys();
        ids.sort();
        bool progress = false;
        for (const QString &id : std::as_const(ids)) {
            const QcContentPackInfo &info = pending[id].info;
            QString problem;
            bool waiting = false;
            for (const QString &dependency : info.dependencies) {
                if (loaded.contains(dependency)) {
                    const QVersionNumber minimum = info.minimumVersions.value(dependency);
                    if (!minimum.isNull() && loaded[dependency] < minimum) {
                        problem = u"needs '%1' %2 or newer, found %3"_s.arg(dependency, minimum.toString(),
                                                                           loaded[dependency].toString());
                    }
                } else if (notLoadedBecause.contains(dependency)) {
                    problem = u"depends on '%1', which %2"_s.arg(dependency, notLoadedBecause[dependency]);
                } else if (!pending.contains(dependency)) {
                    problem = u"missing dependency '%1'"_s.arg(dependency);
                } else {
                    waiting = true;
                }
                if (!problem.isEmpty()) {
                    break;
                }
            }
            if (!problem.isEmpty()) {
                fail(id, problem);
                progress = true;
                break;
            }
            if (!waiting) {
                QcModStatus status = pending.take(id);
                status.state = QcModStatus::State::Loaded;
                loaded.insert(id, QcContentPackLoader::parseVersion(status.info.version));
                ordered.append(status);
                progress = true;
                break;
            }
        }
        if (!progress) {
            // Everything left waits on each other.
            ids = pending.keys();
            ids.sort();
            for (const QString &id : std::as_const(ids)) {
                fail(id, u"dependency cycle among: %1"_s.arg(ids.join(u", "_s)));
            }
        }
    }
    std::ranges::sort(others, {}, [](const QcModStatus &status) { return status.info.directory; });
    return ordered + others;
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
    result.content = std::make_shared<QcContentRegistry>();
    const QcResult<QcContentPackInfo> base = QcBaseContent::loadInto(*result.content, warnings);
    if (!base) {
        return qcError(base.error());
    }
    QByteArray buffer;
    if (const QString problem = clientProblem(*result.content, buffer); !problem.isEmpty()) {
        return qcError(u"base game: "_s + problem);
    }
    result.mods = discover(modsDirectory);
    QSet<QString> failedDuringLoad;
    const auto fail = [&](QcModStatus &mod, const QString &reason) {
        mod.state = QcModStatus::State::Failed;
        mod.reason = reason;
        failedDuringLoad.insert(mod.info.id);
        warnings.append(u"mod %1 not loaded: %2"_s.arg(mod.info.id, reason));
    };
    qint64 readBytes = 0;
    // Registry after each loaded mod; copies share their data, so keeping them is cheap.
    QList<std::pair<qsizetype, QcContentRegistry>> history;
    for (qsizetype index = 0; index < result.mods.size(); ++index) {
        QcModStatus &mod = result.mods[index];
        if (mod.state != QcModStatus::State::Loaded) {
            warnings.append(u"mod %1 not loaded: %2"_s.arg(mod.info.id.isEmpty() ? mod.info.directory : mod.info.id,
                                                         mod.state == QcModStatus::State::Disabled ? u"disabled"_s : mod.reason));
            continue;
        }
        const auto failedDependency = std::ranges::find_if(mod.info.dependencies, [&failedDuringLoad](const QString &id) {
            return failedDuringLoad.contains(id);
        });
        if (failedDependency != mod.info.dependencies.end()) {
            fail(mod, u"depends on '%1', which failed"_s.arg(*failedDependency));
            continue;
        }
        // Bytes on disk (hard links to one big file count each time) are checked before anything is
        // read, against what is left for all mods together; then the mod is loaded into a copy, so
        // a mod that turns out too big leaves no trace.
        const qint64 size = QcContentPackLoader::measure(mod.info.directory);
        if (readBytes + size > totalReadBudget || size > contentSyncBudget) {
            fail(mod, u"the mod's files exceed the %1 MiB left for mods"_s.arg((totalReadBudget - readBytes) / (qint64(1024) * 1024)));
            continue;
        }
        readBytes += size;
        QcContentRegistry candidate = *result.content;
        const QcResult<QcContentPackInfo> info = QcContentPackLoader::load(mod.info.directory, candidate, mod.warnings);
        QString problem = info ? worldgenProblem(candidate) : info.error();
        if (problem.isEmpty() && encodedSize(candidate, buffer) > contentSyncBudget) {
            problem = u"the content of all packs together exceeds %1 MiB"_s.arg(QcProtocol::maxFrameBytes / (1024 * 1024));
        }
        if (!problem.isEmpty()) {
            fail(mod, problem);
            continue;
        }
        candidate.addPack({info->id, info->name, info->version});
        *result.content = std::move(candidate);
        history.append({index, *result.content});
        for (const QString &warning : std::as_const(mod.warnings)) {
            warnings.append(u"mod %1: %2"_s.arg(mod.info.id, warning));
        }
    }

    // One full client check at the end. If it fails, go back to the last state clients accept; the
    // mod after it is the culprit, the later ones are dropped with it.
    if (const QString problem = clientProblem(*result.content, buffer); !problem.isEmpty()) {
        qsizetype good = history.size() - 1;
        // `reason` always describes the state history[good].
        QString reason = problem;
        while (good >= 0) {
            const QString earlier = good > 0 ? clientProblem(history[good - 1].second, buffer) : QString();
            if (earlier.isEmpty()) {
                break;
            }
            reason = earlier;
            --good;
        }
        // history[good] is the first state clients reject; everything from that mod on is dropped.
        for (qsizetype i = good; i < history.size(); ++i) {
            QcModStatus &mod = result.mods[history[i].first];
            mod.state = QcModStatus::State::Failed;
            mod.reason = i == good ? reason : u"loaded after a mod whose content clients reject"_s;
            warnings.append(u"mod %1 not loaded: %2"_s.arg(mod.info.id, mod.reason));
        }
        if (good > 0) {
            *result.content = history[good - 1].second;
        } else {
            // Even the first mod was too much: start again from the base game alone.
            result.content = std::make_shared<QcContentRegistry>();
            QStringList ignored;
            if (const QcResult<QcContentPackInfo> again = QcBaseContent::loadInto(*result.content, ignored); !again) {
                return qcError(again.error());
            }
        }
    }
    result.content->createBlockItems();
    checkReferences(*result.content, warnings);
    return result;
}
