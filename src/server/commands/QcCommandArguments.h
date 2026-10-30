// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

struct QcServerClient;
class QcServerState;

/*! \brief Parsing of command words shared by the commands.

    Everything a command reads is checked here or by the command before it changes anything. */
namespace QcCommandArguments {

/*! Block coordinates a command accepts (block areas, chunks): the block columns of the chunks
    within the technical bound of the world (decision E-883). Teleports accept x and z in the
    range bodies stay in (QcWorld::lowestBody, QcWorld::highestBody). \qcshared */
inline constexpr int lowestBlock = -QcWorld::maxHorizontalBlock;
inline constexpr int highestBlock = QcWorld::maxChunkBlock;
/*! Lowest height a teleport accepts: down into the void, where a player dies (decision E-288). */
inline constexpr double lowestTeleportY = -128.0;

/*! Returns \a name with a namespace: names without one mean the base game's ("qtcraft:"). */
QString qualified(const QString &name);

/*! Returns the block coordinate \a word within [lowestBlock, highestBlock]: a whole number, or
    with \a origin also "~" and "~<n>" relative to it; std::nullopt for anything else, however
    large. */
[[nodiscard]] std::optional<int> blockCoordinate(const QString &word, std::optional<int> origin);
/*! Returns the three block coordinates in \a words starting at index \a first, relative to
    \a origin where they use "~"; std::nullopt when one is invalid, missing or outside
    [lowestBlock, highestBlock] (the caller checks the height). */
[[nodiscard]] std::optional<QcBlockPos> blockPosition(const QStringList &words, qsizetype first, std::optional<QcBlockPos> origin);
/*! Returns the point for teleporting in \a words starting at index \a first: finite numbers, "~"
    relative to \a origin, x and z within the body range (QcWorld::lowestBody, QcWorld::highestBody)
    and y from lowestTeleportY to the top of the world; std::nullopt when one is invalid or
    missing. */
[[nodiscard]] std::optional<QcVec3> point(const QStringList &words, qsizetype first, std::optional<QcVec3> origin);
/*! Returns the whole number at \a index of \a words within [\a low, \a high], or \a fallback when
    the word is missing; std::nullopt for anything else. */
[[nodiscard]] std::optional<int> count(const QStringList &words, qsizetype index, int fallback, int low, int high);
/*! Returns \a word read as "true|on" or "false|off"; std::nullopt for anything else. */
[[nodiscard]] std::optional<bool> toggle(const QString &word);

/*! Returns the joined players of \a state whose name matches \a name (cleaned, without case);
    usually one, more after duplicate names (decision E-232). */
QList<QcServerClient *> players(const QcServerState &state, const QString &name);

} // namespace QcCommandArguments
