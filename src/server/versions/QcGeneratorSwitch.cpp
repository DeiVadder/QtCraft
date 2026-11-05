// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/versions/QcGeneratorSwitch.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/content/worldgen/QcDimensionDefinition.h"
#include "common/world/QcGeneratorVersionMap.h"
#include "common/world/QcWorldInfo.h"
#include "server/dimensions/QcDimensions.h"
#include "server/storage/QcServerPersistence.h"
#include "server/storage/QcWorldStorage.h"
#include "server/versions/QcExploredArea.h"
#include "server/versions/QcVersionMapFile.h"
#include "worldgen/generators/QcWorldGenerators.h"

#include <QDir>
#include <QLockFile>

using namespace Qt::StringLiterals;

namespace {

// Why `info` cannot switch `dimension` to `version`; none when it can.
QcStatus checkSwitch(const QcWorldInfo &info, const QcDimensionDefinition &dimension, int version)
{
    if (!QcDimensions::isVersioned(info)) {
        return qcError(u"A showcase world always generates with the newest generator; it has no version to switch"_s);
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
        info.dimensionGeneratorVersions.insert(dimension.name, version);
    }
    return info;
}

// Switches `dimension` of the locked world in `worldDirectory` (see switchDimension).
QcResult<QcGeneratorSwitch::Switched> switchLocked(const QString &worldDirectory, const QcDimensionDefinition &dimension, int version,
                                                    const std::shared_ptr<const QcContentRegistry> &content)
{
    // First: it finishes a save that broke off after its journal, which may write world.json.
    QcResult<QcExploredArea::Explored> explored = QcExploredArea::of(worldDirectory, dimension, *content);
    if (!explored) {
        return qcError(explored.error());
    }
    const QcWorldStorage storage(worldDirectory, content);
    const QcResult<QcWorldInfo> info = storage.readInfo();
    if (!info) {
        return qcError(info.error());
    }
    if (const QcStatus allowed = checkSwitch(*info, dimension, version); !allowed) {
        return qcError(allowed.error());
    }

    const QString directory = QcWorldStorage::dimensionDirectory(worldDirectory, dimension);
    QcResult<QcGeneratorVersionMap> map = QcVersionMapFile::read(directory);
    if (!map) {
        return qcError(map.error());
    }
    const int from = QcDimensions::versionIn(*info, dimension);
    const qsizetype kept = map->mark(explored->chunks, from);
    if (kept > 0) {
        if (const QcStatus written = QcVersionMapFile::write(directory, *map); !written) {
            return qcError(written.error());
        }
    }
    if (const QcStatus written = storage.writeInfo(withVersion(*info, dimension, version)); !written) {
        return qcError(written.error());
    }
    return QcGeneratorSwitch::Switched{from, version, kept, explored->warnings};
}

} // namespace

namespace QcGeneratorSwitch {

QcResult<Switched> switchDimension(const QString &worldDirectory, const QString &dimension, int version,
                                   const std::shared_ptr<const QcContentRegistry> &content)
{
    // Content without dimensions/ has the default overworld (QcWorldgenCatalog::overworld).
    const bool overworld = dimension == QLatin1StringView(QcDimensionDefinition::overworldName);
    const QcDimensionDefinition *definition = overworld ? &content->worldgen().overworld() : content->worldgen().dimension(dimension);
    if (definition == nullptr) {
        return qcError(u"There is no dimension '%1'"_s.arg(dimension));
    }
    if (!QDir(worldDirectory).exists(QString::fromLatin1(QcWorldStorage::infoFileName))) {
        return qcError(u"There is no world in %1"_s.arg(worldDirectory));
    }

    // Like the server (QcServerPersistence::lockWorld): a lock goes stale only with its process.
    QLockFile lock(QDir(worldDirectory).filePath(QcServerPersistence::lockFileName.toString()));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) {
        return qcError(u"The world %1 is open in QtCraft or cannot be locked; it can only be switched while it is closed"_s.arg(worldDirectory));
    }
    return switchLocked(worldDirectory, *definition, version, content);
}

} // namespace QcGeneratorSwitch
