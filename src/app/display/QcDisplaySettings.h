// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/display/QcFrameLimiter.h"
#include "app/display/QcWindowPlacement.h"
#include "clientcore/settings/QcDisplayOptions.h"

#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QStringList>
#include <QTimer>
#include <QtQmlIntegration/qqmlintegration.h>

class QcKeyControls;
class QScreen;

// The display options (GameController.settings.display, decision E-835): window mode, screen,
// VSync and the frame rate limits, stored in settings.ini (display/<key>) as soon as they change.
// The window follows them once main() hands it over (showWindow()); scripted runs (--background,
// offscreen, the window hooks) keep their window as it is and draw without a limit. The fullscreen
// key works in the menus here and in the world through QcGameInput.
class QcDisplaySettings : public QObject
{
    Q_OBJECT
    // DisplaySettings in QML, for its enumerations (DisplaySettings.WindowMode.Fullscreen).
    QML_NAMED_ELEMENT(DisplaySettings)
    QML_UNCREATABLE("Provided by GameController.settings.display")
    Q_PROPERTY(QcDisplaySettings::WindowMode windowMode READ windowMode WRITE setWindowMode NOTIFY changed FINAL)
    // The screens as the settings list them; `screen` is an index into them.
    Q_PROPERTY(QStringList screens READ screens NOTIFY screensChanged FINAL)
    Q_PROPERTY(int screen READ screen WRITE setScreen NOTIFY changed FINAL)
    // Takes effect at the next start (restartNeeded until then).
    Q_PROPERTY(bool vsync READ vsync WRITE setVsync NOTIFY changed FINAL)
    Q_PROPERTY(bool restartNeeded READ restartNeeded NOTIFY changed FINAL)
    Q_PROPERTY(QcDisplaySettings::FrameLimit frameLimit READ frameLimit WRITE setFrameLimit NOTIFY changed FINAL)
    // In the menus, while paused and while the window is in the background.
    Q_PROPERTY(QcDisplaySettings::FrameLimit backgroundFrameLimit READ backgroundFrameLimit WRITE setBackgroundFrameLimit NOTIFY
                   changed FINAL)
    // Frames per second in force now (0: none), for the debug display.
    Q_PROPERTY(int activeFrameLimit READ activeFrameLimit NOTIFY activeFrameLimitChanged FINAL)
    // The window follows the settings (false in scripted runs).
    Q_PROPERTY(bool controlsWindow READ controlsWindow NOTIFY controlsWindowChanged FINAL)

public:
    // QcDisplayOptions::WindowMode for QML, in its order.
    enum class WindowMode { Windowed, Borderless, Fullscreen };
    Q_ENUM(WindowMode)
    // The value is the number of frames per second (0: no limit).
    enum class FrameLimit { Unlimited = 0, Fps30 = 30, Fps60 = 60, Fps90 = 90, Fps120 = 120, Fps144 = 144 };
    Q_ENUM(FrameLimit)

    // Reads the stored options; `keys` names the fullscreen key.
    explicit QcDisplaySettings(const QcKeyControls &keys, QObject *parent = nullptr);
    ~QcDisplaySettings() override;
    Q_DISABLE_COPY_MOVE(QcDisplaySettings)

    // Shows `window` (not made yet) with the stored VSync, in the stored mode on the stored screen,
    // and limits its frames from now on.
    void showWindow(QQuickWindow &window);
    bool controlsWindow() const { return m_window != nullptr; }

    WindowMode windowMode() const { return static_cast<WindowMode>(m_options.windowMode); }
    void setWindowMode(WindowMode mode);
    QStringList screens() const { return QcWindowPlacement::screenLabels(); }
    int screen() const;
    void setScreen(int index);
    bool vsync() const { return m_options.vsync; }
    void setVsync(bool on);
    bool restartNeeded() const { return m_options.vsync != m_vsyncInEffect; }
    FrameLimit frameLimit() const { return static_cast<FrameLimit>(m_options.frameLimit); }
    void setFrameLimit(FrameLimit limit);
    FrameLimit backgroundFrameLimit() const { return static_cast<FrameLimit>(m_options.backgroundFrameLimit); }
    void setBackgroundFrameLimit(FrameLimit limit);
    int activeFrameLimit() const { return m_frames.limit(); }
    const QcDisplayOptions &options() const { return m_options; }

    // Playing in the foreground (the world shown, no menu, the window active): the game's limit;
    // otherwise the background limit.
    void setForeground(bool foreground);
    // The pacer of the world's frames (QcFrameDriver).
    QcFrameLimiter &frames() { return m_frames; }
    // Fullscreen, or back to the mode before it.
    Q_INVOKABLE void toggleFullscreen();
    // The fullscreen key outside the world (the world's input takes it first while playing).
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void changed();
    void screensChanged();
    void activeFrameLimitChanged();
    void controlsWindowChanged();

private:
    // Switches to `mode`, remembering the windowed frame and the mode fullscreen returns to.
    void switchMode(QcDisplayOptions::WindowMode mode);
    // The platform switched fullscreen (macOS: green button, Globe+F).
    void followFullscreen(bool fullscreen);
    // A frame limit from QML; std::nullopt (with a warning) for a value that is none of the choices.
    [[nodiscard]] static std::optional<int> checkedLimit(FrameLimit limit, const char *setting);
    // Puts the window into the mode of m_options (QcWindowPlacement::apply); the window's own
    // reports meanwhile are no frames of the player's.
    void place();
    // The window moved or changed its size: noted once it rests (the platform's steps of a switch,
    // such as the macOS fullscreen animation, come in a quick series and end in another state).
    void onWindowMoved();
    // A screen went away: the window on it, or meant for it, goes to the screen it falls back to.
    void onScreenRemoved(const QScreen *screen);
    // Remembers the window's frame and screen while it is windowed.
    void noteWindowedFrame();
    void updateLimit();
    void save() const;

    const QcKeyControls &m_keys;
    QcDisplayOptions m_options;
    // VSync of the window (swap interval 1, Qt's default, unless showWindow() made it without).
    bool m_vsyncInEffect = true;
    bool m_foreground = false;
    QPointer<QQuickWindow> m_window;
    QcWindowPlacement m_placement;
    // While place() runs.
    bool m_placing = false;
    // Notes the windowed frame once the window rests (onWindowMoved).
    QTimer m_noteTimer;
    QcFrameLimiter m_frames;
};
