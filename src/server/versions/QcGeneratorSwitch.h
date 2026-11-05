// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"

#include <QString>
#include <QStringList>

#include <memory>

class QcContentRegistry;

/*! \brief Switches a dimension of a closed world to another generator version (decisions E-1010,
    E-1011), the core of the switch the menu and qtcraft-headless offer: everything generated so
    far keeps the version it was generated with, only chunks never generated get the new one.

    The explored area (QcExploredArea) not yet in the dimension's version map
    (QcVersionMapFile) gets the dimension's present version there; then world.json names the new
    version. The map is written first: a switch that breaks off before world.json leaves the
    world at its old version, with a map naming that same version, so it looks as before. Holds
    the world's lock throughout; no backup and no progress (the callers' part). */
namespace QcGeneratorSwitch {

/*! \brief What a switch did. \qcfields */
struct Switched
{
    /*! The version the dimension had and the one it has now. \qcshared */
    int from = 0;
    int to = 0;
    /*! The chunks that joined the version map at \c {from}. */
    qsizetype keptChunks = 0;
    /*! For the player: maps that could not be read, so their area did not count. */
    QStringList warnings;
};

/*! Switches the dimension \a dimension of the world in \a worldDirectory to generator version
    \a version, with \a content (its dimensions, the blocks of the maps). Returns an error, with
    the world unchanged, for a world that is open, a showcase world (always current), an unknown
    dimension, a version its generator does not make or the version it has, and for files that
    cannot be read or written. */
[[nodiscard]] QcResult<Switched> switchDimension(const QString &worldDirectory, const QString &dimension, int version,
                                                 const std::shared_ptr<const QcContentRegistry> &content);

} // namespace QcGeneratorSwitch
