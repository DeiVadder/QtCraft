// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worlds/QcGeneratorUpgrade.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/content/worldgen/QcDimensionDefinition.h"
#include "server/backup/QcWorldBackup.h"
#include "server/dimensions/QcDimensions.h"
#include "server/storage/QcChunkStore.h"
#include "server/storage/QcServerPersistence.h"
#include "server/storage/QcWorldStorage.h"
#include "server/versions/QcGeneratorSwitch.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worlds/QcWorldsLogging.h"

#include <QDir>
#include <QFile>
#include <QLockFile>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

using QcGeneratorUpgrade::Change;
using QcGeneratorUpgrade::Converted;

// The switch of the dimension `name` with the generator type `type` from `version`, if its
// generator makes a newer one.
std::optional<Change> changeOf(const QString &name, const QString &type, int version)
{
    const QcGeneratorVersion::Series series = QcWorldGenerators::versionSeries(type);
    if (version >= series.current) {
        return std::nullopt;
    }
    return Change{name, version, series.current};
}

// The lock of the world in `directory` (the server's, QcServerPersistence::lockWorld), held by the
// caller from the check before the backup to the end of the switch, so that no server opens the
// world in between; null when the world is open (its lock held by a running process).
std::unique_ptr<QLockFile> lockWorld(const QString &directory)
{
    auto lock = std::make_unique<QLockFile>(QDir(directory).filePath(QcServerPersistence::lockFileName.toString()));
    lock->setStaleLockTime(0);
    return lock->tryLock(0) ? std::move(lock) : nullptr;
}

// The refusal of an open world.
QString openRefusal(const QString &directory)
{
    return u"The world %1 is open in QtCraft or cannot be locked; it can only be switched while it is closed"_s.arg(directory);
}

// Removes the backup a stopped conversion made: it would only be a copy of the unchanged world.
Converted cancelled(const QString &backup)
{
    if (!backup.isEmpty() && !QDir(backup).removeRecursively()) {
        qCWarning(lcWorlds) << "Could not remove the backup of a cancelled generator switch:" << backup;
    }
    return Converted{{}, 0, 0, {}, true};
}

// The backup of `request`'s world below its root, labelled after the newest version it gets.
QcResult<QcWorldBackup::Summary> backUp(const QcGeneratorUpgrade::Request &request)
{
    const int newest = std::ranges::max(request.changes, {}, &Change::to).to;
    const QString root = request.backupRoot.isEmpty() ? QcWorldBackup::defaultRoot(request.worldDirectory) : request.backupRoot;
    return QcWorldBackup::copy(request.worldDirectory, root, QcGeneratorUpgrade::backupLabel(newest), QDateTime::currentDateTime());
}

// Backs up and switches the world of `request`, whose lock the caller holds (see convert()).
QcResult<Converted> convertLocked(const QcGeneratorUpgrade::Request &request, const QList<QcGeneratorSwitch::Target> &targets,
                                  const std::shared_ptr<const QcContentRegistry> &content, const QcConversionControl &control)
{
    control.report({QcConversionControl::Stage::Backup, 0, 1});
    const QcResult<QcWorldBackup::Summary> backup = backUp(request);
    if (!backup) {
        return qcError(u"The world was not switched: its backup failed: %1"_s.arg(backup.error()));
    }
    control.report({QcConversionControl::Stage::Backup, 1, 1});
    if (control.stopped()) {
        return cancelled(backup->directory);
    }

    const QcResult<QcGeneratorSwitch::Outcome> outcome = QcGeneratorSwitch::switchLockedWorld(request.worldDirectory, targets, content, control);
    if (!outcome) {
        return qcError(u"%1 (the backup is in %2)"_s.arg(outcome.error(), backup->directory));
    }
    if (outcome->cancelled) {
        return cancelled(backup->directory);
    }
    Converted converted{backup->directory, 0, 0, {}, false};
    for (const QcGeneratorSwitch::Switched &switched : outcome->switched) {
        converted.keptChunks += switched.keptChunks;
        converted.ringChunks += switched.ringChunks;
        converted.warnings.append(switched.warnings);
    }
    qCInfo(lcWorlds) << "Switched the world generator of" << request.worldDirectory << "keeping" << converted.keptChunks << "chunks and a ring of"
                     << converted.ringChunks << "chunks; backup" << converted.backup;
    return converted;
}

// Sets the kept generator version (QcWorldInfo::keepGeneratorVersion) in world.json of the closed
// world in `directory` to `version`, writing it only when it changes. Returns the version it had,
// or an error for an open world or files that cannot be read or written.
QcResult<int> setKeptVersion(const QString &directory, const std::shared_ptr<const QcContentRegistry> &content, int version)
{
    const std::unique_ptr<QLockFile> lock = lockWorld(directory);
    if (!lock) {
        return qcError(openRefusal(directory));
    }
    const QcWorldStorage storage(directory, content);
    QcResult<QcWorldInfo> info = storage.readInfo();
    if (!info) {
        return qcError(info.error());
    }
    const int before = info->keepGeneratorVersion;
    if (before == version) {
        return before;
    }
    info->keepGeneratorVersion = version;
    if (const QcStatus written = storage.writeInfo(*info); !written) {
        return qcError(written.error());
    }
    return before;
}

} // namespace

namespace QcGeneratorUpgrade {

QList<Change> outdatedIn(const QcWorldInfo &info, const QcContentRegistry &content)
{
    QList<Change> changes;
    if (!QcDimensions::isVersioned(info)) {
        return changes;
    }
    if (const std::optional<Change> overworld = changeOf(QString::fromLatin1(QcDimensionDefinition::overworldName), info.generator,
                                                         info.generatorVersion)) {
        changes.append(*overworld);
    }
    for (auto it = info.dimensionGeneratorVersions.cbegin(); it != info.dimensionGeneratorVersions.cend(); ++it) {
        const QcDimensionDefinition *dimension = content.worldgen().dimension(it.key());
        // A number of another series (E-1033) says nothing about this generator; the switch refuses it.
        if (dimension == nullptr || dimension->isOverworld() || !QcDimensions::seriesFits(info, *dimension)) {
            continue;
        }
        if (const std::optional<Change> change = changeOf(it.key(), QcDimensions::generatorTypeIn(info, *dimension), it.value())) {
            changes.append(*change);
        }
    }
    return changes;
}

bool reachBeyondOverworld(const QList<Change> &changes)
{
    const QString overworld = QString::fromLatin1(QcDimensionDefinition::overworldName);
    return std::ranges::any_of(changes, [&overworld](const Change &change) { return change.dimension != overworld; });
}

QcResult<Converted> convert(const Request &request, const std::shared_ptr<const QcContentRegistry> &content, const QcConversionControl &control)
{
    if (request.mode == Mode::Blend && !QcBlendWidth::isValid(request.blendChunks)) {
        return qcError(u"A transition is %1 to %2 chunks wide, not %3"_s.arg(QcBlendWidth::minChunks).arg(QcBlendWidth::maxChunks).arg(request.blendChunks));
    }
    if (request.changes.isEmpty()) {
        return qcError(u"Nothing to switch"_s);
    }
    const std::unique_ptr<QLockFile> lock = lockWorld(request.worldDirectory);
    if (!lock) {
        return qcError(openRefusal(request.worldDirectory));
    }
    QList<QcGeneratorSwitch::Target> targets;
    for (const Change &change : request.changes) {
        targets.append({change.dimension, change.to, request.mode == Mode::Blend ? request.blendChunks : 0});
    }
    if (const QcStatus finished = QcGeneratorSwitch::finishPendingSave(request.worldDirectory); !finished) {
        return qcError(finished.error());
    }
    // Refused before the backup, which would be a copy for nothing.
    if (const QcStatus checked = QcGeneratorSwitch::check(request.worldDirectory, targets, content); !checked) {
        return qcError(checked.error());
    }
    if (control.stopped()) {
        return cancelled({});
    }
    return convertLocked(request, targets, content, control);
}

QcWorldInfo settledInfo(const QString &worldDirectory, const QcWorldInfo &info, const std::shared_ptr<const QcContentRegistry> &content)
{
    if (!QFile::exists(QDir(worldDirectory).filePath(QcChunkStore::journalName.toString()))) {
        return info;
    }
    const std::unique_ptr<QLockFile> lock = lockWorld(worldDirectory);
    if (!lock) {
        return info;
    }
    if (const QcStatus finished = QcGeneratorSwitch::finishPendingSave(worldDirectory); !finished) {
        qCWarning(lcWorlds) << "Cannot finish the save of" << worldDirectory << "that broke off:" << finished.error();
        return info;
    }
    const QcResult<QcWorldInfo> settled = QcWorldStorage(worldDirectory, content).readInfo();
    return settled ? *settled : info;
}

bool isDeclined(const QcWorldInfo &info, const QList<Change> &changes)
{
    return std::ranges::all_of(changes, [&info](const Change &change) { return change.to <= info.keepGeneratorVersion; });
}

QcStatus keepForGood(const QString &worldDirectory, const QList<Change> &changes, const std::shared_ptr<const QcContentRegistry> &content)
{
    if (changes.isEmpty()) {
        return {};
    }
    const QcResult<int> kept = setKeptVersion(worldDirectory, content, std::ranges::max(changes, {}, &Change::to).to);
    if (!kept) {
        return qcError(kept.error());
    }
    return {};
}

int askAgain(const QStringList &worldDirectories, const std::shared_ptr<const QcContentRegistry> &content)
{
    int forgotten = 0;
    for (const QString &directory : worldDirectories) {
        const QcResult<int> had = setKeptVersion(directory, content, 0);
        if (!had) {
            qCWarning(lcWorlds) << "Cannot ask about the world generator of" << directory << "again:" << had.error();
        } else if (*had > 0) {
            ++forgotten;
        }
    }
    return forgotten;
}

QString modeName(Mode mode)
{
    return mode == Mode::Blend ? u"blend"_s : u"hard"_s;
}

std::optional<Mode> modeNamed(QStringView name)
{
    if (name == u"hard"_s) {
        return Mode::Hard;
    }
    if (name == u"blend"_s) {
        return Mode::Blend;
    }
    return std::nullopt;
}

QString backupLabel(int version)
{
    return u"before-generator-%1"_s.arg(version);
}

} // namespace QcGeneratorUpgrade
