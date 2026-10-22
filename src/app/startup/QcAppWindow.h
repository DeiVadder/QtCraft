#pragma once

#include "common/core/QcResult.h"

#include <memory>

class QcGameController;
class QcOffscreenRender;
class QQuickWindow;
struct QcAppOptions;

// The game's main window at start-up (decision E-530): colour scheme and control palette, the
// screen and size it opens with, menu choices preselected by test hooks, and scripted runs that
// keep in the background or render offscreen (E-229).
class QcAppWindow
{
public:
    // The game screens are drawn dark over the world (E-079), so the controls are dark too, whatever
    // the system appearance. QTCRAFT_COLOR_SCHEME=light or =system is a test hook for comparison;
    // =system also keeps the system's control palette instead of the game's (E-279).
    static void applyColorScheme();
    // Once the window's scene graph runs (decision E-834): prepares the render thread for crash
    // reports and logs the graphics API and adapter.
    static void watchRenderThread(QQuickWindow &window);
    // Scripted runs keep out of the user's way; offscreen runs are scripted runs too.
    static void prepareScriptedRun(const QcAppOptions &options, bool offscreen);
    // Shows the main window (in the player's display settings unless a hook placed it), or moves
    // its content into an offscreen renderer (returned). The error when offscreen rendering cannot
    // start.
    [[nodiscard]] static QcResult<std::unique_ptr<QcOffscreenRender>> startWindowOrOffscreen(QQuickWindow &window, QcGameController &game,
                                                                                           const QcAppOptions &options, bool offscreen);
};
