#pragma once

#include <QList>
#include <QMargins>
#include <QRect>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>

class QSettings;

// How the game window shows and how often it draws (decision E-835): the window mode, the screen it
// covers, the window's place in windowed mode, VSync and the frame rate limits for playing and for
// menus, a paused game or a window in the background. Stored in settings.ini as display/<key> and
// read before the window opens. Values that make no sense give the default, with a warning.
struct QcDisplayOptions
{
    // A window with frame, a frameless window covering its screen, or the platform's fullscreen
    // (on macOS a fullscreen space of its own).
    enum class WindowMode { Windowed, Borderless, Fullscreen };
    // Frame rate limits on offer, frames per second; 0 is no limit.
    static constexpr std::array<int, 6> frameLimits{0, 30, 60, 90, 120, 144};
    static constexpr int defaultFrameLimit = 0;
    static constexpr int defaultBackgroundFrameLimit = 30;

    WindowMode windowMode = WindowMode::Windowed;
    // The mode F11 returns to from fullscreen (windowed or borderless).
    WindowMode previousMode = WindowMode::Windowed;
    // Name of the screen (QScreen::name); empty: the primary screen.
    QString screen;
    // The window's frame in windowed mode, in virtual desktop coordinates; empty: centred on the
    // screen in its default size.
    QRect windowGeometry;
    // Swap interval 1; takes effect when the window is made (the next start).
    bool vsync = true;
    // Most frames per second while playing; 0 is no limit (with VSync: the display's rate).
    int frameLimit = defaultFrameLimit;
    // Most frames per second in the menus, while paused and while the window is in the background;
    // never more than frameLimit. 0 is no limit beyond frameLimit.
    int backgroundFrameLimit = defaultBackgroundFrameLimit;

    // "windowed", "borderless", "fullscreen".
    static QString modeName(WindowMode mode);
    [[nodiscard]] static std::optional<WindowMode> modeFromName(QStringView name);
    // Whether `framesPerSecond` is one of frameLimits.
    static bool isFrameLimit(int framesPerSecond);

    // The limit in force: frameLimit while playing in the foreground, else the lower of both
    // (0 counts as no limit).
    int activeFrameLimit(bool foreground) const;
    // Fullscreen, or back to the mode before it (F11).
    WindowMode toggledFullscreen() const;

    // The index of the screen called `name` among `names`; empty `name`: std::nullopt (the caller
    // takes the primary screen), unknown names too.
    [[nodiscard]] static std::optional<qsizetype> screenIndex(const QStringList &names, const QString &name);
    // Where a windowed window goes on a screen with this available area: the stored frame when its
    // centre lies on the screen, else `defaultSize` centred. The frames are client areas;
    // `frameMargins` (title bar and border, QWindow::frameMargins) stay inside the available area
    // too, so the title bar can still be grabbed: the frame is shrunk and moved to fit.
    static QRect windowedFrame(QRect stored, QRect available, QSize defaultSize, QMargins frameMargins = {});

    static QcDisplayOptions load(const QSettings &file);
    // The file at `path`; missing or unreadable: the defaults.
    static QcDisplayOptions load(const QString &path);
    void save(QSettings &file) const;

    friend bool operator==(const QcDisplayOptions &, const QcDisplayOptions &) = default;
};
