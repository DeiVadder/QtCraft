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

// --icon: writes the icon set of the base game to `directory` (the icon must not depend on the mods
// of whoever builds the game); false when the base game cannot be loaded.
bool exportIcon(const QString &directory);

// Runs the tool the command line asks for with `content` and `config` (seed, generator); false when
// it asks for none. Builds the world generator first and prints its warnings in any case.
bool run(const QCommandLineParser &parser, const QcHeadlessOptions::Options &options, const QcServerConfig &config,
         const std::shared_ptr<const QcContentRegistry> &content);

} // namespace QcHeadlessTools
