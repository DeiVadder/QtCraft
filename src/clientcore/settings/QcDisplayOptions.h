// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QMargins>
#include <QRect>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>

class QSettings;

/*! \brief How the game window shows and how often it draws (decision E-835): the window mode, the
    screen it covers, the window's place in windowed mode, VSync and the frame rate limits for
    playing and for menus, a paused game or a window in the background.

    Stored in settings.ini as display/<key> and read before the window opens. Values that make no
    sense give the default, with a warning. */
struct QcDisplayOptions
{
    /*! \brief A window with frame, a frameless window covering its screen, or the platform's
        fullscreen (on macOS a fullscreen space of its own). */
    enum class WindowMode {
        Windowed,   // a window with frame
        Borderless, // a frameless window covering its screen
        Fullscreen  // the platform's fullscreen
    };
    /*! Frame rate limits on offer, frames per second; 0 is no limit. */
    static constexpr std::array<int, 6> frameLimits{0, 30, 60, 90, 120, 144};
    /*! The limit while playing of new settings: none. */
    static constexpr int defaultFrameLimit = 0;
    /*! The limit in the menus and in the background of new settings. */
    static constexpr int defaultBackgroundFrameLimit = 30;

    /*! Windowed, borderless or fullscreen. */
    WindowMode windowMode = WindowMode::Windowed;
    /*! The mode F11 returns to from fullscreen (windowed or borderless). */
    WindowMode previousMode = WindowMode::Windowed;
    /*! Name of the screen (QScreen::name); empty: the primary screen. */
    QString screen;
    /*! The window's frame in windowed mode, in virtual desktop coordinates; empty: centred on
        the screen in its default size. */
    QRect windowGeometry;
    /*! Swap interval 1; takes effect when the window is made (the next start). */
    bool vsync = true;
    /*! Most frames per second while playing; 0 is no limit (with VSync: the display's rate). */
    int frameLimit = defaultFrameLimit;
    /*! Most frames per second in the menus, while paused and while the window is in the
        background; never more than frameLimit. 0 is no limit beyond frameLimit. */
    int backgroundFrameLimit = defaultBackgroundFrameLimit;

    /*! Returns the name of \a mode in settings.ini: "windowed", "borderless" or "fullscreen". */
    static QString modeName(WindowMode mode);
    /*! Returns the mode named \a name (modeName()); nothing for an unknown name. */
    [[nodiscard]] static std::optional<WindowMode> modeFromName(QStringView name);
    /*! Returns whether \a framesPerSecond is one of frameLimits. */
    static bool isFrameLimit(int framesPerSecond);

    /*! Returns the limit in force: frameLimit while playing in the \a foreground, else the lower of
        both (0 counts as no limit). */
    int activeFrameLimit(bool foreground) const;
    /*! Returns the mode F11 switches to: fullscreen, or back to the mode before it (F11). */
    WindowMode toggledFullscreen() const;

    /*! Returns the index of the screen called \a name among \a names; empty \a name:
        std::nullopt (the caller takes the primary screen), unknown names too. */
    [[nodiscard]] static std::optional<qsizetype> screenIndex(const QStringList &names, const QString &name);
    /*! Returns where a windowed window goes on a screen with the \a available area: the
        \a stored frame when its centre lies on the screen, else \a defaultSize centred. The
        frames are client areas; \a frameMargins (title bar and border, QWindow::frameMargins)
        stay inside the available area too, so the title bar can still be grabbed: the frame is
        shrunk and moved to fit. */
    static QRect windowedFrame(QRect stored, QRect available, QSize defaultSize, QMargins frameMargins = {});

    /*! Returns the options stored in \a file; defaults for values that make no sense. */
    static QcDisplayOptions load(const QSettings &file);
    /*! Returns the file at \a path; missing or unreadable: the defaults. */
    static QcDisplayOptions load(const QString &path);
    /*! Stores the options in \a file. */
    void save(QSettings &file) const;

    friend bool operator==(const QcDisplayOptions &, const QcDisplayOptions &) = default;
};
