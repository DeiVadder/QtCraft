// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "headless/QcHeadlessOptions.h"
#include "server/core/QcServerConfig.h"

#include <QCommandLineParser>
#include <QString>

#include <memory>

class QcContentRegistry;

/*! \brief The one-shot tools of \c {qtcraft-headless} that start no world and exit when done:
    the app icon, the sounds, the handbook (Markdown and web), texture packs and atlas
    statistics, the showcase tour and the world generation tools (--world-stats, --slice-map,
    --biome-map, --map). */
namespace QcHeadlessTools {

/*! Ends the application with \a code once the event loop runs. */
void exitSoon(int code);

/*! --export-icon: writes the icon of the base game to \a path, a Windows .ico file when the path
    ends in .ico, else a macOS .iconset directory (the icon must not depend on the mods of
    whoever builds the game); returns false when the base game cannot be loaded. */
bool exportIcon(const QString &path);

/*! Returns whether the command line (\a options as \a parser read them) asks for a world
    generation tool (--world-stats, --slice-map, --biome-map, --map); these draw a terrain
    world. */
[[nodiscard]] bool wantsWorldTool(const QCommandLineParser &parser, const QcHeadlessOptions::Options &options);

/*! Runs the tool the command line (\a options as \a parser read them) asks for with \a content
    and \a config (seed, world options, generator version); returns false when it asks for none. The
    content tools come first; otherwise it builds the terrain generator, prints its warnings and
    runs the world tool, if any. */
bool run(const QCommandLineParser &parser, const QcHeadlessOptions::Options &options, const QcServerConfig &config,
         const std::shared_ptr<const QcContentRegistry> &content);

} // namespace QcHeadlessTools
