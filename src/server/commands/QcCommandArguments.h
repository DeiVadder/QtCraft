#pragma once

#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

struct QcServerClient;
class QcServerState;

// Parsing of command words shared by the commands. Everything a command reads is checked here or
// by the command before it changes anything.
namespace QcCommandArguments {

// Block coordinates a command accepts (block areas, chunks): the block columns of the chunks within
// the technical bound of the world (decision E-883). Teleports accept x and z in the range bodies
// stay in (QcWorld::lowestBody, QcWorld::highestBody).
inline constexpr int lowestBlock = -QcWorld::maxHorizontalBlock;
inline constexpr int highestBlock = QcWorld::maxChunkBlock;
// Lowest height a teleport accepts: down into the void, where a player dies (decision E-288).
inline constexpr double lowestTeleportY = -128.0;

// Names without a namespace mean the base game's.
QString qualified(const QString &name);

// A block coordinate within [lowestBlock, highestBlock]: a whole number, or with `origin` also "~" and "~<n>"
// relative to it; std::nullopt for anything else, however large.
[[nodiscard]] std::optional<int> blockCoordinate(const QString &word, std::optional<int> origin);
// Three block coordinates starting at `words[first]`, relative to `origin` where they use "~";
// std::nullopt when one is invalid, missing or outside [lowestBlock, highestBlock] (the caller checks the height).
[[nodiscard]] std::optional<QcBlockPos> blockPosition(const QStringList &words, qsizetype first, std::optional<QcBlockPos> origin);
// A point for teleporting: finite numbers, "~" relative to `origin`, x and z within the body range
// (QcWorld::lowestBody, QcWorld::highestBody) and y from lowestTeleportY to the top of the world.
[[nodiscard]] std::optional<QcVec3> point(const QStringList &words, qsizetype first, std::optional<QcVec3> origin);
// A whole number within [low, high], or `fallback` when the word is missing.
[[nodiscard]] std::optional<int> count(const QStringList &words, qsizetype index, int fallback, int low, int high);
// "true|on" or "false|off".
[[nodiscard]] std::optional<bool> toggle(const QString &word);

// Joined players whose name matches (cleaned, without case); usually one, more after duplicate
// names (decision E-232).
QList<QcServerClient *> players(const QcServerState &state, const QString &name);

} // namespace QcCommandArguments
