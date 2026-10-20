#pragma once

#include <QList>
#include <QRect>

// Platform side of scripted runs (benchmarks, screenshots, profiling): where the built-in display
// is, and keeping the game in the background so it never takes focus from the user.
class QcScriptedRun
{
public:
    // Global geometries (points) of displays built into the computer; empty where unknown.
    static QList<QRect> builtInDisplays();
    // Before the application object exists: a scripted run (QTCRAFT_OFFSCREEN=1, QTCRAFT_BACKGROUND=1
    // or --background) keeps Qt from turning the process into a foreground application, which
    // would take the focus from the user's window for a moment at every start (macOS); a no-op
    // elsewhere and for normal starts.
    static void keepOutOfForeground(int argc, char *argv[]);
    // Makes the application an accessory that does not activate itself or show in the Dock
    // (macOS); a no-op elsewhere. Call before the first window is shown.
    static void stayInBackground();
    // Keeps the system from throttling the timers of an application without a shown window (App
    // Nap on macOS) for the rest of the run; a no-op elsewhere.
    static void keepTimersRunning();
};
