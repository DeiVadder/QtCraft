// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/startup/QcAppWindow.h"

#include "app/controllers/QcGameController.h"
#include "app/controllers/QcNewWorldDraft.h"
#include "app/controllers/QcWorldOptionsController.h"
#include "app/debug/QcAppLogging.h"
#include "app/hooks/QcMenuPreselect.h"
#include "app/hooks/QcOffscreenRender.h"
#include "app/hooks/QcScriptedRun.h"
#include "app/models/QcControlPalette.h"
#include "app/startup/QcAppOptions.h"
#include "clientcore/settings/QcScreenChoice.h"
#include "clientcore/settings/QcUiPalette.h"
#include "common/diagnostics/QcCrashHandler.h"

#include <QGuiApplication>
#include <QIcon>
#include <QQuickStyle>
#include <QQuickWindow>
#include <rhi/qrhi.h>
#include <QScreen>
#include <QStyleHints>

#include <atomic>
#include <memory>

using namespace Qt::StringLiterals;

namespace {

// QTCRAFT_COLOR_SCHEME=system (test hook) keeps the system's colour scheme and control palette
// instead of the game's (E-279).
bool systemColorScheme()
{
    return qEnvironmentVariable("QTCRAFT_COLOR_SCHEME") == u"system"_s;
}

// Which description of the window's QRhi the log still owes: none, the first one or one made anew
// after a lost device.
enum class DeviceLog { None, Created, Recreated };

// The graphics API Qt Quick draws with, for the log.
QString graphicsApiName()
{
    switch (QQuickWindow::graphicsApi()) {
    case QSGRendererInterface::Direct3D11:
        return u"Direct3D 11"_s;
    case QSGRendererInterface::Direct3D12:
        return u"Direct3D 12"_s;
    case QSGRendererInterface::Vulkan:
        return u"Vulkan"_s;
    case QSGRendererInterface::Metal:
        return u"Metal"_s;
    case QSGRendererInterface::OpenGL:
        return u"OpenGL"_s;
    case QSGRendererInterface::Software:
        return u"software rendering"_s;
    default:
        return u"graphics API %1"_s.arg(static_cast<int>(QQuickWindow::graphicsApi()));
    }
}

// Logs the backend and GPU of `rhi`, made at the start or anew after a lost device.
void describeDevice(const QRhi &rhi, DeviceLog log)
{
    const QRhiDriverInfo driver = rhi.driverInfo();
    const QString prefix = log == DeviceLog::Recreated ? u"Graphics: QRhi recreated: "_s : u"Graphics: "_s;
    qCInfo(lcStartup).noquote() << prefix
                                       + u"%1 on %2 (device id 0x%3, vendor id 0x%4)"_s.arg(QString::fromLatin1(rhi.backendName()),
                                                                                           QString::fromUtf8(driver.deviceName))
                                             .arg(driver.deviceId, 0, 16)
                                             .arg(driver.vendorId, 0, 16);
}

// Popups, dialogs and tooltips take their colours from their window (E-279).
void applyControlPalette(QQuickWindow &window)
{
    if (!systemColorScheme() && !QcControlPalette::applyTo(window)) {
        qCWarning(lcStartup, "The window offers no control palette; controls keep the system colours");
    }
}

// Scripted runs open on a fixed screen (the built-in display), not where the mouse happens to be.
void placeWindow(QQuickWindow *window, const QString &screenRequest)
{
    const QList<QScreen *> screens = QGuiApplication::screens();
    QList<QcScreenChoice::Screen> descriptions;
    const QList<QRect> builtIn = QcScriptedRun::builtInDisplays();
    for (const QScreen *screen : screens) {
        descriptions.append({screen->name(), screen->geometry(), builtIn.contains(screen->geometry()), screen == QGuiApplication::primaryScreen()});
    }
    if (const std::optional<QcScreenChoice::Result> chosen = QcScreenChoice::choose(descriptions, screenRequest)) {
        QScreen *screen = screens[chosen->index];
        window->setScreen(screen);
        window->setGeometry(QcScreenChoice::placeInside(screen->availableGeometry(), window->size()));
        qCInfo(lcStartup).noquote() << u"Window on screen %1 \"%2\" (%3)"_s.arg(chosen->index).arg(screen->name(), chosen->reason);
    }
}

// QTCRAFT_WINDOW_SIZE=<width>x<height> (test hook): screenshots of small windows. The window's
// minimum size still applies.
void applyWindowSize(QQuickWindow &window)
{
    const QStringList size = qEnvironmentVariable("QTCRAFT_WINDOW_SIZE").split(u'x');
    if (size.size() == 2 && size[0].toInt() > 0 && size[1].toInt() > 0) {
        window.resize(QSize(size[0].toInt(), size[1].toInt()).expandedTo(window.minimumSize()));
    }
}

} // namespace

void QcAppWindow::applyColorScheme()
{
    if (!systemColorScheme()) {
        const bool light = qEnvironmentVariable("QTCRAFT_COLOR_SCHEME") == u"light"_s;
        QGuiApplication::styleHints()->setColorScheme(light ? Qt::ColorScheme::Light : Qt::ColorScheme::Dark);
    }
}

void QcAppWindow::applyAppearance()
{
    applyColorScheme();
    applyControlStyle();
    applyWindowIcon();
}

void QcAppWindow::applyWindowIcon()
{
#if !defined(Q_OS_MACOS)
    const QIcon icon(u":/icons/QtCraft.ico"_s);
    if (icon.availableSizes().isEmpty()) {
        qCWarning(lcStartup, "The window icon cannot be read (image format plugin qico missing?); the system's default icon stays");
        return;
    }
    QGuiApplication::setWindowIcon(icon);
#endif
}

void QcAppWindow::applyControlStyle()
{
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle(u"Basic"_s);
    }

    const QString theme = qEnvironmentVariable("QTCRAFT_THEME");
    if (theme.isEmpty()) {
        return;
    }
    if (const std::optional<QcUiPalette::Preset> preset = QcUiPalette::presetFromName(theme)) {
        QcUiPalette::setPreset(*preset);
    } else {
        qCWarning(lcStartup) << "QTCRAFT_THEME" << theme << ": no such colour variant (A, B or C); the default one (C) stays";
    }
}

void QcAppWindow::prepareScriptedRun(const QcAppOptions &options, bool offscreen)
{
    if (options.background || offscreen) {
        QcScriptedRun::stayInBackground();
    }
    if (offscreen) {
        QcScriptedRun::keepTimersRunning();
        QcOffscreenRender::prepare();
    }
}

QcResult<std::unique_ptr<QcOffscreenRender>> QcAppWindow::startWindowOrOffscreen(QQuickWindow &window, QcGameController &game,
                                                                                const QcAppOptions &options, bool offscreen)
{
    if (options.background || offscreen) {
        window.setFlag(Qt::WindowDoesNotAcceptFocus, true);
        // Above other windows: macOS stops drawing a window that others cover completely, and a
        // benchmark or screenshot behind the user's windows would get no frames.
        window.setFlag(Qt::WindowStaysOnTopHint, true);
        game.input()->setBackgroundRun(true);
    }
    applyWindowSize(window);
    applyControlPalette(window);
    placeWindow(&window, options.screen);
    QcMenuPreselect::newWorld(*game.worldOptions()->draft(), qEnvironmentVariable("QTCRAFT_MENU_WORLD_TYPE"), qEnvironmentVariable("QTCRAFT_MENU_GAME_MODE"));

    if (!offscreen) {
        // The player's window mode, screen and frame limits (decision E-835), unless a scripted run
        // or a window hook (--screen, QTCRAFT_WINDOW_SIZE) placed the window.
        const bool placedByHook = options.background || !options.screen.isEmpty() || qEnvironmentVariableIsSet("QTCRAFT_WINDOW_SIZE");
        if (placedByHook) {
            window.show();
        } else {
            game.settings()->display()->showWindow(window);
        }
        return nullptr;
    }
    game.preview()->setOffscreen(true);
    // The window is never shown: QTCRAFT_WINDOW_SIZE may be larger than the screen it was placed
    // on (a 2560 x 1440 monitor's fullscreen on a MacBook, decision E-875).
    applyWindowSize(window);
    // Created after placing: it takes the window's size and screen.
    auto render = std::make_unique<QcOffscreenRender>(window);
    applyControlPalette(*render->window());
    if (!render->start()) {
        return std::unexpected(u"Cannot render offscreen"_s);
    }
    return render;
}

void QcAppWindow::watchRenderThread(QQuickWindow &window)
{
    // All on the render thread. While the window lives, Qt Quick invalidates its scene graph and
    // initialises it again only when the graphics device was lost or reset: then the window's QRhi
    // is made anew, and it may be set only by the first frame after sceneGraphInitialized.
    const auto invalidated = std::make_shared<std::atomic_bool>(false);
    const auto undescribed = std::make_shared<std::atomic<DeviceLog>>(DeviceLog::None);
    QObject::connect(&window, &QQuickWindow::sceneGraphInvalidated, &window, [invalidated] { invalidated->store(true); }, Qt::DirectConnection);
    // The render thread gets its alternate signal stack for crash reports here, its first moment
    // in QtCraft's hands.
    QObject::connect(
        &window, &QQuickWindow::sceneGraphInitialized, &window,
        [&window, invalidated, undescribed] {
            QcCrashHandler::prepareThread();
            const DeviceLog log = invalidated->exchange(false) ? DeviceLog::Recreated : DeviceLog::Created;
            if (log == DeviceLog::Recreated) {
                qCWarning(lcStartup).noquote() << u"Graphics: the device was lost or reset; recreating the QRhi on %1"_s.arg(graphicsApiName());
            }
            if (const QRhi *rhi = window.rhi()) {
                describeDevice(*rhi, log);
            } else if (QQuickWindow::graphicsApi() == QSGRendererInterface::Software) {
                qCInfo(lcStartup, "Graphics: no QRhi (software rendering)");
            } else {
                undescribed->store(log);
            }
        },
        Qt::DirectConnection);
    QObject::connect(
        &window, &QQuickWindow::beforeRendering, &window,
        [&window, undescribed] {
            const DeviceLog log = undescribed->exchange(DeviceLog::None);
            if (log == DeviceLog::None) {
                return;
            }
            if (const QRhi *rhi = window.rhi()) {
                describeDevice(*rhi, log);
            } else {
                qCWarning(lcStartup).noquote() << u"Graphics: no QRhi on %1"_s.arg(graphicsApiName());
            }
        },
        Qt::DirectConnection);
}
