// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"
#include "common/world/QcConversionControl.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <memory>

class QcContentRegistry;

/*! \brief Switches dimensions of a closed world to other generator versions (decisions E-1010,
    E-1011, E-1013), the core of the switch the menu and qtcraft-headless offer: everything
    generated so far keeps the version it was generated with, only chunks never generated get the
    new one.

    The explored area (QcExploredArea) not yet in the dimension's version map (QcVersionMapFile)
    gets the dimension's present version there, together with every chunk a structure built into
    it reaches (QcStructureClosure), so such a structure keeps its old plan; then world.json names
    the new versions. The maps are written first: a switch that breaks off before world.json
    leaves the world at its old versions, with maps naming those same versions, so it looks as
    before. Holds the world's lock throughout; no backup (the callers' part, QcGeneratorUpgrade). */
namespace QcGeneratorSwitch {

/*! \brief What a switch did to one dimension. \qcfields */
struct Switched
{
    /*! The dimension's name. */
    QString dimension;
    /*! The version the dimension had and the one it has now. \qcshared */
    int from = 0;
    int to = 0;
    /*! The chunks that joined the version map at \c {from}: the explored ones and those of the
        structures reaching out of them. */
    qsizetype keptChunks = 0;
    /*! For the player: maps that could not be read, so their area did not count. */
    QStringList warnings;
};

/*! \brief One dimension to switch and its new version. \qcfields */
struct Target
{
    QString dimension;
    int version = 0;
};

/*! \brief What a switch of several dimensions did: one entry per target, or none and
    \c {cancelled} when it was stopped before writing (the world unchanged). \qcfields */
struct Outcome
{
    QList<Switched> switched;
    bool cancelled = false;
};

/*! Switches the dimensions \a targets of the world in \a worldDirectory, with \a content (its
    dimensions, the blocks of the maps, the generators), reporting to \a control (stages Survey
    and Write) and stopping when it says so before anything is written. Returns an error, with the
    world unchanged, for a world that is open, a showcase world (always current), an unknown or
    repeated dimension, a version its generator does not make or the version it has, and for
    files that cannot be read; a write that fails leaves the world as described in the class. */
[[nodiscard]] QcResult<Outcome> switchDimensions(const QString &worldDirectory, const QList<Target> &targets,
                                                 const std::shared_ptr<const QcContentRegistry> &content, const QcConversionControl &control);

/*! Switches \a targets of the world in \a worldDirectory with \a content, reporting to
    \a control, as switchDimensions() does, for a caller that holds the world's lock
    (QcServerPersistence::lockFileName) itself, from before its backup to the end
    (QcGeneratorUpgrade::convert). Returns what it did or the error. */
[[nodiscard]] QcResult<Outcome> switchLockedWorld(const QString &worldDirectory, const QList<Target> &targets,
                                                  const std::shared_ptr<const QcContentRegistry> &content, const QcConversionControl &control);

/*! Returns why switching \a targets of the world in \a worldDirectory with \a content would be
    refused from what its world.json says (as switchDimensions() refuses, without the lock and the
    files of its chunks); none when it may be tried. Changes nothing. */
[[nodiscard]] QcStatus check(const QString &worldDirectory, const QList<Target> &targets, const std::shared_ptr<const QcContentRegistry> &content);

/*! Switches the dimension \a dimension of the world in \a worldDirectory to generator version
    \a version with \a content, as switchDimensions() without control. Returns what it did or the
    error. */
[[nodiscard]] QcResult<Switched> switchDimension(const QString &worldDirectory, const QString &dimension, int version,
                                                 const std::shared_ptr<const QcContentRegistry> &content);

} // namespace QcGeneratorSwitch
