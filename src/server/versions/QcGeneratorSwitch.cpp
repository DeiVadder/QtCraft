// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/versions/QcGeneratorSwitch.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/content/worldgen/QcDimensionDefinition.h"
#include "common/world/QcBlendWidth.h"
#include "common/world/QcGeneratorVersionMap.h"
#include "common/world/QcWorldInfo.h"
#include "server/dimensions/QcDimensions.h"
#include "server/storage/QcChunkCodec.h"
#include "server/storage/QcChunkStore.h"
#include "server/storage/QcServerPersistence.h"
#include "server/storage/QcWorldStorage.h"
#include "server/versions/QcExploredArea.h"
#include "server/versions/QcVersionMapFile.h"
#include "worldgen/blending/QcRingBlender.h"
#include "worldgen/generators/QcStructureClosure.h"
#include "worldgen/generators/QcVersionedGenerator.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worldgen/generators/QcWorldOptions.h"

#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QSet>

#include <algorithm>
#include <functional>

using namespace Qt::StringLiterals;

namespace {

using QcGeneratorSwitch::Outcome;
using QcGeneratorSwitch::Switched;
using QcGeneratorSwitch::Target;

// Why `info` cannot switch `dimension` to `version`; none when it can.
QcStatus checkSwitch(const QcWorldInfo &info, const QcDimensionDefinition &dimension, int version)
{
    if (!QcDimensions::isVersioned(info)) {
        return qcError(u"A showcase world always generates with the newest generator; it has no version to switch"_s);
    }
    if (const QcStatus fits = QcDimensions::seriesFits(info, dimension); !fits) {
        return fits;
    }
    const QcGeneratorVersion::Series series = QcWorldGenerators::versionSeries(QcDimensions::generatorTypeIn(info, dimension));
    if (!series.isKnown(version)) {
        return qcError(u"The dimension '%1' cannot be generated as version %2; this game generates versions %3 to %4"_s.arg(dimension.name)
                           .arg(version)
                           .arg(series.oldest)
                           .arg(series.current));
    }
    if (QcDimensions::versionIn(info, dimension) == version) {
        return qcError(u"The dimension '%1' is generated as version %2 already"_s.arg(dimension.name).arg(version));
    }
    if (!dimension.isOverworld() && !info.dimensionGeneratorVersions.contains(dimension.name)
        && info.dimensionGeneratorVersions.size() >= QcWorldInfo::maxDimensionVersions) {
        return qcError(u"The world keeps the versions of %1 dimensions already"_s.arg(info.dimensionGeneratorVersions.size()));
    }
    return {};
}

// `info` with `version` for `dimension`.
QcWorldInfo withVersion(QcWorldInfo info, const QcDimensionDefinition &dimension, int version)
{
    if (dimension.isOverworld()) {
        info.generatorVersion = version;
    } else {
        return QcDimensions::withVersion(std::move(info), dimension, version);
    }
    return info;
}

// The dimension `name` of `content`; null without one. Content without dimensions/ has the default
// overworld (QcWorldgenCatalog::overworld).
const QcDimensionDefinition *definitionOf(const QcContentRegistry &content, const QString &name)
{
    return name == QLatin1StringView(QcDimensionDefinition::overworldName) ? &content.worldgen().overworld() : content.worldgen().dimension(name);
}

// A dimension ready to be written: its definition, the folder of its map, the map and the report.
struct Prepared
{
    const QcDimensionDefinition *definition = nullptr;
    QString directory;
    QcGeneratorVersionMap map;
    Switched switched;
    // The generator of the dimension as it is before the switch (survey()).
    std::shared_ptr<const QcVersionedGenerator> generator;
    // The blended chunks of a switch with a transition (withRing()).
    QList<QcRingBlender::Blended> ring;
};

// Makes generators of the versions of a dimension.
using GeneratorFactory = std::function<std::unique_ptr<QcWorldGenerator>(int version)>;

// What makes the generators of `dimension`: its type, the world's options and seed.
GeneratorFactory factoryOf(const QcWorldInfo &info, const QcDimensionDefinition &dimension, const std::shared_ptr<const QcContentRegistry> &content)
{
    const QString options = dimension.isOverworld() ? info.generatorOptions : dimension.generatorOptions;
    return [applied = QcWorldOptions::apply(content, info.generator, info.worldOptions).content, seed = info.seed,
            type = QcDimensions::generatorTypeIn(info, dimension), options](int version) {
        return QcWorldGenerators::create(type, applied, seed, options, version);
    };
}

// The generator of `dimension` as the server opens `info` (QcDimensions::open,
// QcServerPersistence::applyVersionMap): its version there, the world's options and seed, the map.
QcResult<std::unique_ptr<QcVersionedGenerator>> generatorOf(const QcWorldInfo &info, const QcDimensionDefinition &dimension,
                                                            const QcGeneratorVersionMap &map, const std::shared_ptr<const QcContentRegistry> &content)
{
    const GeneratorFactory create = factoryOf(info, dimension, content);
    std::unique_ptr<QcWorldGenerator> base = create(QcDimensions::versionIn(info, dimension));
    if (!base) {
        return qcError(u"The dimension '%1' cannot be generated with the world type '%2'"_s.arg(dimension.name, QcDimensions::generatorTypeIn(info, dimension)));
    }
    auto generator = std::make_unique<QcVersionedGenerator>(std::move(base));
    if (!map.isEmpty()) {
        if (const QcStatus set = generator->setVersionMap(map, create); !set) {
            return qcError(set.error());
        }
    }
    return generator;
}

// `target` of the world `info` in `worldDirectory` with its map and its generator before the
// switch, checked; survey() adds what it keeps.
QcResult<Prepared> prepare(const QString &worldDirectory, const QcWorldInfo &info, const Target &target,
                           const std::shared_ptr<const QcContentRegistry> &content)
{
    const QcDimensionDefinition &dimension = *definitionOf(*content, target.dimension);
    if (const QcStatus allowed = checkSwitch(info, dimension, target.version); !allowed) {
        return qcError(allowed.error());
    }
    Prepared prepared{&dimension, QcWorldStorage::dimensionDirectory(worldDirectory, dimension), {}, {}, {}, {}};
    QcResult<QcGeneratorVersionMap> map = QcVersionMapFile::read(prepared.directory);
    if (!map) {
        return qcError(map.error());
    }
    QcResult<std::unique_ptr<QcVersionedGenerator>> generator = generatorOf(info, dimension, *map, content);
    if (!generator) {
        return qcError(generator.error());
    }
    const int from = QcDimensions::versionIn(info, dimension);
    prepared.switched = {dimension.name, from, target.version, 0, 0, {}};
    prepared.map = *std::move(map);
    prepared.generator = *std::move(generator);
    return prepared;
}

// `prepared` with the explored chunks without a version in its map and the reach of every structure
// they touch kept at the old version (decision E-1013); none when `control` stopped it.
std::optional<Prepared> survey(Prepared prepared, const QcExploredArea::Explored &explored, const QcConversionControl &control)
{
    const std::optional<QcChunkSet> kept = QcStructureClosure::of(*prepared.generator, explored.chunks.without(prepared.map.allChunks()), control);
    if (!kept) {
        return std::nullopt;
    }
    prepared.switched.keptChunks = prepared.map.mark(*kept, prepared.switched.from);
    prepared.switched.warnings = explored.warnings;
    return prepared;
}

// `prepared` with a ring `widthChunks` wide around its old chunks blended into `target` and marked
// in its map (decision E-1040); none when `control` stopped it.
QcResult<std::optional<Prepared>> withRing(Prepared prepared, int widthChunks, const QcWorldInfo &info, const std::shared_ptr<const QcContentRegistry> &content,
                                           const QcConversionControl &control)
{
    const std::optional<QcBlendField> field = QcRingBlender::plan(*prepared.generator, prepared.map.allChunks(), widthChunks, control);
    if (!field) {
        return std::optional<Prepared>{};
    }
    prepared.switched.ringChunks = prepared.map.mark(field->ring(), prepared.switched.from);
    if (field->isCut()) {
        prepared.switched.warnings.append(u"The ring around the dimension '%1' did not settle around all its structures; some keep only a part of their edge"_s.arg(prepared.switched.dimension));
    }

    // As the server will open the dimension: the new version with the map, so that the ring has
    // the old version's structures that lie wholly in the old version's chunks and no other.
    const QcWorldInfo switched = withVersion(info, *prepared.definition, prepared.switched.to);
    QcResult<std::unique_ptr<QcVersionedGenerator>> older = generatorOf(switched, *prepared.definition, prepared.map, content);
    if (!older) {
        return qcError(older.error());
    }
    const std::unique_ptr<QcWorldGenerator> newer = factoryOf(info, *prepared.definition, content)(prepared.switched.to);
    if (!newer) {
        return qcError(u"The dimension '%1' cannot be generated as version %2"_s.arg(prepared.switched.dimension).arg(prepared.switched.to));
    }
    newer->setStructureFilter([](QcXzRect) { return false; });

    const auto encode = [&content](const QcChunk &chunk) { return QcChunkCodec::encode(chunk, *content); };
    std::optional<QList<QcRingBlender::Blended>> ring = QcRingBlender::blend({**older, *newer}, *field, static_cast<quint64>(info.seed), encode, control);
    if (!ring) {
        return std::optional<Prepared>{};
    }
    prepared.ring = *std::move(ring);
    return std::optional<Prepared>{std::move(prepared)};
}

// Whether a switch of `dimension` in `info` with a transition stores a ring: only the overworld
// blends, and only its terrain differs between versions (flat and void worlds have no edge).
bool blends(const QcWorldInfo &info, const QcDimensionDefinition &dimension)
{
    return dimension.isOverworld() && QcDimensions::generatorTypeIn(info, dimension) == QLatin1StringView(QcWorldGenerators::terrain);
}

// Writes the blended chunks of `prepared` and world.json for `info` as one save of the store in
// `directory` (the world's: only the overworld blends).
QcStatus commitRings(const QString &directory, const QcWorldInfo &info, const QList<Prepared> &prepared)
{
    QcChunkStore store(directory);
    if (const QcStatus opened = store.open(); !opened) {
        return opened;
    }
    QList<QcRingBlender::Blended> ring;
    for (const Prepared &dimension : prepared) {
        ring.append(dimension.ring);
    }
    return QcGeneratorSwitch::commitRing(store, ring, info);
}

// Writes the maps of `prepared` that grew, then world.json with the new versions, with the chunks
// of a ring in the same save (see the namespace).
QcStatus write(const QcWorldStorage &storage, QcWorldInfo info, const QList<Prepared> &prepared, const QcConversionControl &control)
{
    const auto total = static_cast<qint64>(prepared.size()) + 1;
    qint64 done = 0;
    for (const Prepared &dimension : prepared) {
        control.report({QcConversionControl::Stage::Write, done++, total});
        if (dimension.switched.keptChunks > 0 || dimension.switched.ringChunks > 0) {
            if (const QcStatus written = QcVersionMapFile::write(dimension.directory, dimension.map); !written) {
                return written;
            }
        }
        info = withVersion(std::move(info), *dimension.definition, dimension.switched.to);
    }
    control.report({QcConversionControl::Stage::Write, done, total});
    const bool hasRing = std::ranges::any_of(prepared, [](const Prepared &dimension) { return !dimension.ring.isEmpty(); });
    if (const QcStatus written = hasRing ? commitRings(storage.directory(), info, prepared) : storage.writeInfo(info); !written) {
        return written;
    }
    control.report({QcConversionControl::Stage::Write, total, total});
    return {};
}

// Switches `targets` of the locked world in `worldDirectory` (see switchDimensions).
QcResult<Outcome> switchLocked(const QString &worldDirectory, const QList<Target> &targets, const std::shared_ptr<const QcContentRegistry> &content,
                               const QcConversionControl &control)
{
    // First: it finishes a save that broke off after its journal, which may write world.json.
    QList<QcExploredArea::Explored> explored;
    for (const Target &target : targets) {
        QcResult<QcExploredArea::Explored> area = QcExploredArea::of(worldDirectory, *definitionOf(*content, target.dimension), *content);
        if (!area) {
            return qcError(area.error());
        }
        explored.append(*std::move(area));
    }
    const QcWorldStorage storage(worldDirectory, content);
    const QcResult<QcWorldInfo> info = storage.readInfo();
    if (!info) {
        return qcError(info.error());
    }

    QList<Prepared> prepared;
    for (qsizetype i = 0; i < targets.size(); ++i) {
        QcResult<Prepared> dimension = prepare(worldDirectory, *info, targets.at(i), content);
        if (!dimension) {
            return qcError(dimension.error());
        }
        std::optional<Prepared> surveyed = survey(*std::move(dimension), explored.at(i), control);
        if (!surveyed) {
            return Outcome{{}, true};
        }
        const int blendChunks = targets.at(i).blendChunks;
        if (blendChunks > 0 && blends(*info, *surveyed->definition)) {
            QcResult<std::optional<Prepared>> blended = withRing(*std::move(surveyed), blendChunks, *info, content, control);
            if (!blended) {
                return qcError(blended.error());
            }
            surveyed = *std::move(blended);
            if (!surveyed) {
                return Outcome{{}, true};
            }
        } else if (blendChunks > 0 && !surveyed->definition->isOverworld()) {
            surveyed->switched.warnings.append(u"The dimension '%1' has no transition: only the overworld blends; it got a hard border"_s.arg(targets.at(i).dimension));
        }
        prepared.append(*std::move(surveyed));
    }
    if (control.stopped()) {
        return Outcome{{}, true};
    }
    if (const QcStatus written = write(storage, *info, prepared, control); !written) {
        return qcError(written.error());
    }
    Outcome outcome;
    for (const Prepared &dimension : std::as_const(prepared)) {
        outcome.switched.append(dimension.switched);
    }
    return outcome;
}

// Why `targets` cannot be switched in `worldDirectory` with `content` before anything is read; none
// when they can.
QcStatus checkTargets(const QString &worldDirectory, const QList<Target> &targets, const QcContentRegistry &content)
{
    if (targets.isEmpty()) {
        return qcError(u"No dimension to switch"_s);
    }
    QSet<QString> seen;
    for (const Target &target : targets) {
        if (target.blendChunks != 0 && !QcBlendWidth::isValid(target.blendChunks)) {
            return qcError(u"A transition is %1 to %2 chunks wide, not %3"_s.arg(QcBlendWidth::minChunks).arg(QcBlendWidth::maxChunks).arg(target.blendChunks));
        }
        if (definitionOf(content, target.dimension) == nullptr) {
            return qcError(u"There is no dimension '%1'"_s.arg(target.dimension));
        }
        if (seen.contains(target.dimension)) {
            return qcError(u"The dimension '%1' is named twice"_s.arg(target.dimension));
        }
        seen.insert(target.dimension);
    }
    if (!QDir(worldDirectory).exists(QString::fromLatin1(QcWorldStorage::infoFileName))) {
        return qcError(u"There is no world in %1"_s.arg(worldDirectory));
    }
    return {};
}

} // namespace

namespace QcGeneratorSwitch {

QcResult<Outcome> switchDimensions(const QString &worldDirectory, const QList<Target> &targets, const std::shared_ptr<const QcContentRegistry> &content,
                                   const QcConversionControl &control)
{
    if (const QcStatus checked = checkTargets(worldDirectory, targets, *content); !checked) {
        return qcError(checked.error());
    }

    // Like the server (QcServerPersistence::lockWorld): a lock goes stale only with its process.
    QLockFile lock(QDir(worldDirectory).filePath(QcServerPersistence::lockFileName.toString()));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) {
        return qcError(u"The world %1 is open in QtCraft or cannot be locked; it can only be switched while it is closed"_s.arg(worldDirectory));
    }
    return switchLocked(worldDirectory, targets, content, control);
}

QcResult<Outcome> switchLockedWorld(const QString &worldDirectory, const QList<Target> &targets, const std::shared_ptr<const QcContentRegistry> &content,
                                    const QcConversionControl &control)
{
    if (const QcStatus checked = checkTargets(worldDirectory, targets, *content); !checked) {
        return qcError(checked.error());
    }
    return switchLocked(worldDirectory, targets, content, control);
}

QcStatus commitRing(QcChunkStore &store, const QList<QcRingBlender::Blended> &ring, const QcWorldInfo &info)
{
    for (const QcRingBlender::Blended &chunk : ring) {
        store.put(QcChunkStore::Kind::Blocks, chunk.pos, chunk.blob);
        store.noteGenerated(chunk.pos);
    }
    return store.commit({{QString::fromLatin1(QcWorldStorage::infoFileName), QcWorldStorage::infoBytes(info)}});
}

QcStatus finishPendingSave(const QString &worldDirectory)
{
    if (!QFile::exists(QDir(worldDirectory).filePath(QcChunkStore::journalName.toString()))) {
        return {};
    }
    QcChunkStore store(worldDirectory);
    return store.open();
}

QcStatus check(const QString &worldDirectory, const QList<Target> &targets, const std::shared_ptr<const QcContentRegistry> &content)
{
    if (const QcStatus checked = checkTargets(worldDirectory, targets, *content); !checked) {
        return checked;
    }
    const QcResult<QcWorldInfo> info = QcWorldStorage(worldDirectory, content).readInfo();
    if (!info) {
        return qcError(info.error());
    }
    for (const Target &target : targets) {
        if (const QcStatus checked = checkSwitch(*info, *definitionOf(*content, target.dimension), target.version); !checked) {
            return checked;
        }
    }
    return {};
}

QcResult<Switched> switchDimension(const QString &worldDirectory, const QString &dimension, int version,
                                   const std::shared_ptr<const QcContentRegistry> &content)
{
    const QcResult<Outcome> outcome = switchDimensions(worldDirectory, {Target{dimension, version}}, content, {});
    if (!outcome) {
        return qcError(outcome.error());
    }
    return outcome->switched.constFirst();
}

} // namespace QcGeneratorSwitch
