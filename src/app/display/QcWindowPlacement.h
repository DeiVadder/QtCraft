#pragma once

#include "clientcore/settings/QcDisplayOptions.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QStringList>
#include <QWindow>

class QScreen;

// Puts the game window into a window mode on a screen (decision E-835): windowed with its stored
// frame, borderless (frameless, covering the screen) or fullscreen (QWindow::showFullScreen, on
// macOS a fullscreen space). Notices when the platform switches fullscreen by itself (the green
// button or Globe+F on macOS), so the setting can follow.
class QcWindowPlacement : public QObject
{
    Q_OBJECT

public:
    explicit QcWindowPlacement(QObject *parent = nullptr);

    // The window to place; its size now is the size of a windowed window without a stored frame.
    void setWindow(QWindow *window);
    QWindow *window() const { return m_window; }

    // The screens' names (QScreen::name) in the order of QGuiApplication::screens().
    static QStringList screenNames();
    // The screens as the settings list them: number, name and resolution.
    static QStringList screenLabels();
    // The screen called `name`; the primary screen for an empty or unknown name (unknown: with a
    // warning).
    static QScreen *screenNamed(const QString &name);

    // Shows the window in `requested`'s mode on its screen. Works on a copy: the window's signals
    // during the switch may change the caller's options.
    void apply(const QcDisplayOptions &requested);
    // Shortly after apply(): the platform may still report steps of the switch (state and
    // geometry), which are no changes of the player's.
    bool settling() const;
    // The window's frame while it is windowed and visible, else empty.
    QRect windowedFrame() const;
    // The name of the screen the window is on; empty without a window.
    QString screenName() const;

signals:
    // The platform switched the window into fullscreen or out of it, not apply().
    void fullscreenSwitched(bool fullscreen);

private:
    void onWindowStateChanged(Qt::WindowState state);

    QPointer<QWindow> m_window;
    QSize m_defaultSize;
    // The mode apply() set last.
    QcDisplayOptions::WindowMode m_mode = QcDisplayOptions::WindowMode::Windowed;
    // The fullscreen state the platform reported last.
    bool m_fullscreen = false;
    // Since the last apply().
    QElapsedTimer m_sinceApply;
};
