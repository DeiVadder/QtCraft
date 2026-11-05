// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"
#include "common/world/QcConversionControl.h"
#include "common/world/QcWorldInfo.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

class QcContentRegistry;

/*! \brief Switching a saved world to a newer world generator before it is played (decisions
    E-1013, E-1014): the offer when a world opens and the conversion with its backup, for the
    menu and qtcraft-headless.

    Only chunks never generated get the new version; everything explored keeps its old one, and so
    does every structure an explored chunk started (QcGeneratorSwitch, QcStructureClosure). The
    world is backed up first (QcWorldBackup); a conversion that is cancelled or fails before
    writing leaves the world folder as it was. Keeping the old version is always the default: a
    world is never switched without being asked. */
namespace QcGeneratorUpgrade {

/*! \brief How the border between the old and the new version looks. */
enum class Mode {
    /*! Hard chunk borders, as in the reference game: the new version starts at the first chunk
        never generated; seams are allowed. */
    Hard,
    /*! A ring of chunks blending the old into the new version (etappe 87.3; refused until then). */
    Blend
};

/*! \brief One dimension's switch: its name, the version it has and the version it gets.
    \qcfields */
struct Change
{
    QString dimension;
    int from = 0;
    int to = 0;
};

/*! \brief A conversion to run: the world folder, the switches, the mode and where the backup goes
    (a folder of backups, QcWorldBackup::defaultRoot() when empty). \qcfields */
struct Request
{
    QString worldDirectory;
    QList<Change> changes;
    Mode mode = Mode::Hard;
    QString backupRoot;
};

/*! \brief What a conversion did: the backup folder, the chunks kept at their old version over all
    dimensions and warnings for the player; or \c {cancelled} with the world unchanged and no
    backup left. \qcfields */
struct Converted
{
    QString backup;
    qsizetype keptChunks = 0;
    QStringList warnings;
    bool cancelled = false;
};

/*! Returns the switches \a info offers with \a content: the overworld and every dimension the
    world has generated (its version in world.json) whose version is older than the newest its
    generator makes, each to that newest version; none for a showcase world (always current). */
[[nodiscard]] QList<Change> outdatedIn(const QcWorldInfo &info, const QcContentRegistry &content);
/*! Returns whether the player declined \a changes of the world \a info for good ("don't ask
    again", QcWorldInfo::keepGeneratorVersion): none of them goes beyond the version declined. */
[[nodiscard]] bool isDeclined(const QcWorldInfo &info, const QList<Change> &changes);
/*! Records in world.json of the closed world in \a worldDirectory, read with \a content, that
    its player keeps its versions until a generator newer than the newest of \a changes comes
    (decision E-1014). Returns an error for an open world or files that cannot be read or
    written. */
[[nodiscard]] QcStatus keepForGood(const QString &worldDirectory, const QList<Change> &changes, const std::shared_ptr<const QcContentRegistry> &content);
/*! Forgets "don't ask again" in the closed worlds of \a worldDirectories, read with \a content,
    so the question comes back; open or unreadable worlds are left out with a warning. Returns how
    many worlds had it. */
[[nodiscard]] int askAgain(const QStringList &worldDirectories, const std::shared_ptr<const QcContentRegistry> &content);
/*! Converts the closed world of \a request with \a content (the world's base game and mods),
    reporting to \a control and stopping when it says so: the backup, then the switch of every
    change (QcGeneratorSwitch::switchDimensions). Returns what it did, or an error for the mode
    Blend (not yet), an open world, a backup that cannot be made or a switch that is refused; a
    backup made before an error is kept. */
[[nodiscard]] QcResult<Converted> convert(const Request &request, const std::shared_ptr<const QcContentRegistry> &content,
                                          const QcConversionControl &control);
/*! Returns the name of \a mode on the command line ("hard", "blend"). */
[[nodiscard]] QString modeName(Mode mode);
/*! Returns the mode named \a name (modeName()); none for another word. */
[[nodiscard]] std::optional<Mode> modeNamed(QStringView name);
/*! Returns the label of the backup before switching to \a version ("before-generator-<n>"). */
[[nodiscard]] QString backupLabel(int version);

} // namespace QcGeneratorUpgrade
