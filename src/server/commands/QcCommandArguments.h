#pragma once

#include "QcBlockPos.h"
#include "QcVec3.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

struct QcServerClient;
struct QcServerState;

// Parsing of command words shared by the commands. Everything a command reads is checked here or
// by the command before it changes anything.
namespace QcCommandArguments {

// Horizontal limit of positions a command accepts (teleport, block areas, chunks).
inline constexpr int worldLimit = 30'000'000;
// Lowest height a teleport accepts: down into the void, where a player dies (decision E-288).
inline constexpr double lowestTeleportY = -128.0;

// Names without a namespace mean the base game's.
QString qualified(const QString &name);

// A block coordinate within ±worldLimit: a whole number, or with `origin` also "~" and "~<n>"
// relative to it; std::nullopt for anything else, however large.
std::optional<int> blockCoordinate(const QString &word, std::optional<int> origin);
// Three block coordinates starting at `words[first]`, relative to `origin` where they use "~";
// std::nullopt when one is invalid, missing or outside ±worldLimit (the caller checks the height).
std::optional<QcBlockPos> blockPosition(const QStringList &words, qsizetype first, std::optional<QcBlockPos> origin);
// A point for teleporting: finite numbers, "~" relative to `origin`, inside the world limit and
// from lowestTeleportY to the top of the world.
std::optional<QcVec3> point(const QStringList &words, qsizetype first, std::optional<QcVec3> origin);
// A whole number within [low, high], or `fallback` when the word is missing.
std::optional<int> count(const QStringList &words, qsizetype index, int fallback, int low, int high);
// "true|on" or "false|off".
std::optional<bool> toggle(const QString &word);

// Joined players whose name matches (cleaned, without case); usually one, more after duplicate
// names (decision E-232).
QList<QcServerClient *> players(const QcServerState &state, const QString &name);

} // namespace QcCommandArguments
