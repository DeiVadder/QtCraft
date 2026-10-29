// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

class QcGameController;
class QcOffscreenRender;
class QQuickWindow;
struct QcAppOptions;

// What the game does once its window is up (decision E-530): settings, worlds and mods, the test
// hooks of reproducible scenes, and the scripted parts of a run (audio, startup script, benchmark,
// statistics log, the world, network game or screen to open at once).
class QcAppStartup
{
public:
    // Loads the settings; texture packs for this run only, e.g. for screenshots and benchmarks:
    // comma-separated pack ids, highest priority first, or "none" (decision E-304), set before the
    // settings load them.
    static void loadSettings(QcGameController &game);
    // Worlds, mods and the scripted parts of the run; screenshots come from the offscreen renderer
    // when there is one.
    static void startGame(QcGameController &game, const QcAppOptions &options, QQuickWindow &window, QcOffscreenRender *offscreenRender);
};
