#include "app/startup/QcAppWindow.h"

#include "app/controllers/QcGameController.h"
#include "app/hooks/QcOffscreenRender.h"
#include "app/hooks/QcScriptedRun.h"
#include "app/models/QcControlPalette.h"
#include "app/startup/QcAppOptions.h"
#include "clientcore/settings/QcScreenChoice.h"
#include "common/gameplay/QcGameMode.h"
#include "worldgen/generators/QcWorldGenerators.h"

#include <QGuiApplication>
#include <QQuickWindow>
#include <QScreen>
#include <QStyleHints>

using namespace Qt::StringLiterals;

namespace {

// QTCRAFT_COLOR_SCHEME=system (test hook) keeps the system's colour scheme and control palette
// instead of the game's (E-279).
bool systemColorScheme()
{
    return qEnvironmentVariable("QTCRAFT_COLOR_SCHEME") == u"system"_s;
}

// Popups, dialogs and tooltips take their colours from their window (E-279).
void applyControlPalette(QQuickWindow &window)
{
    if (!systemColorScheme() && !QcControlPalette::applyTo(window)) {
        qWarning("The window offers no control palette; controls keep the system colours");
    }
}

// QTCRAFT_MENU_WORLD_TYPE=flat|void|showcase is a test hook that preselects the world type of the
// menu's new world, so that screenshots show the fields of that type.
void preselectWorldType(const QQuickWindow &window)
{
    const qsizetype index = QcWorldGenerators::types().indexOf(qEnvironmentVariable("QTCRAFT_MENU_WORLD_TYPE"));
    QObject *choice = window.findChild<QObject *>(u"worldTypeChoice"_s);
    if (index >= 0 && choice) {
        choice->setProperty("currentIndex", int(index));
    }

    // QTCRAFT_MENU_GAME_MODE=creative preselects the game mode the same way.
    QObject *mode = window.findChild<QObject *>(u"gameModeChoice"_s);
    if (mode && QcGameModes::parse(qEnvironmentVariable("QTCRAFT_MENU_GAME_MODE")) == QcGameMode::Creative) {
        mode->setProperty("currentIndex", 1);
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
        qInfo().noquote() << u"Window on screen %1 \"%2\" (%3)"_s.arg(chosen->index).arg(screen->name(), chosen->reason);
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
    preselectWorldType(window);

    if (!offscreen) {
        window.show();
        return nullptr;
    }
    game.preview()->setOffscreen(true);
    // Created after placing: it takes the window's size and screen.
    auto render = std::make_unique<QcOffscreenRender>(window);
    applyControlPalette(*render->window());
    if (!render->start()) {
        return std::unexpected(u"Cannot render offscreen"_s);
    }
    return render;
}
