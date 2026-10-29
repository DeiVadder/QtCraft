// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "headless/QcHeadlessOptions.h"
#include "server/core/QcServerConfig.h"

#include <QCommandLineParser>
#include <QString>

#include <memory>

class QcContentRegistry;

// The one-shot tools of `qtcraft-headless` that start no world and exit when done: the app icon,
// the sounds, the handbook (Markdown and web), texture packs and atlas statistics, the showcase tour
// and the world generation tools (--world-stats, --slice-map, --biome-map, --map).
namespace QcHeadlessTools {

// Ends the application with `code` once the event loop runs.
void exitSoon(int code);

// --export-icon: writes the icon of the base game to `path`, a Windows .ico file when the path ends
// in .ico, else a macOS .iconset directory (the icon must not depend on the mods of whoever builds
// the game); false when the base game cannot be loaded.
bool exportIcon(const QString &path);

// Whether the command line asks for a world generation tool (--world-stats, --slice-map,
// --biome-map, --map), which draws a terrain world.
[[nodiscard]] bool wantsWorldTool(const QCommandLineParser &parser, const QcHeadlessOptions::Options &options);

// Runs the tool the command line asks for with `content` and `config` (seed, generator); false when
// it asks for none. Builds the world generator first and prints its warnings in any case.
bool run(const QCommandLineParser &parser, const QcHeadlessOptions::Options &options, const QcServerConfig &config,
         const std::shared_ptr<const QcContentRegistry> &content);

} // namespace QcHeadlessTools
