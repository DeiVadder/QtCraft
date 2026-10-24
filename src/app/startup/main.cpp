#include "app/controllers/QcDiagnosticsSettings.h"
#include "app/controllers/QcGameController.h"
#include "app/debug/QcAppLogging.h"
#include "app/hooks/QcOffscreenRender.h"
#include "app/hooks/QcScriptedRun.h"
#include "app/models/QcItemIconProvider.h"
#include "app/models/QcMapImageProvider.h"
#include "app/models/QcPixelArtProvider.h"
#include "app/models/QcTexturePackPreviewProvider.h"
#include "app/startup/QcAppLanguage.h"
#include "app/startup/QcAppOptions.h"
#include "app/startup/QcAppStartup.h"
#include "app/startup/QcAppWindow.h"
#include "common/core/QcDataRoot.h"
#include "common/diagnostics/QcDiagnosticsSession.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTranslator>
#if defined(QT_QML_DEBUG)
// Profiling builds (QC_QML_DEBUG): the header's static enabler lets qmlprofiler connect.
#include <QtQml/qqmldebug.h>
#endif

#include <memory>
#include <print>

using namespace Qt::StringLiterals;

namespace {

// The image providers the game hands its pictures to; the engine owns and deletes them.
struct ImageProviders
{
    QcItemIconProvider *icons = nullptr;
    QcTexturePackPreviewProvider *texturePackPreviews = nullptr;
    QcMapImageProvider *maps = nullptr;
};

// Adds every image provider of the game to `engine`: item icons, texture pack previews, maps and
// the pixel art of the workbench screens (decision E-884, which needs nothing afterwards).
ImageProviders addImageProviders(QQmlApplicationEngine &engine)
{
    const ImageProviders providers{new QcItemIconProvider, new QcTexturePackPreviewProvider, new QcMapImageProvider};
    engine.addImageProvider(QString::fromLatin1(QcItemIconProvider::providerId), providers.icons);
    engine.addImageProvider(QString::fromLatin1(QcTexturePackPreviewProvider::providerId), providers.texturePackPreviews);
    engine.addImageProvider(QString::fromLatin1(QcMapImageProvider::providerId), providers.maps);
    engine.addImageProvider(QString::fromLatin1(QcPixelArtProvider::providerId), new QcPixelArtProvider);
    return providers;
}

} // namespace

// The steps of the start in their order: command line, scripted-run setup, language, the QML
// engine and its main window, then settings, language switching and the game (decision E-530).
int main(int argc, char *argv[])
{
    QcScriptedRun::keepOutOfForeground(argc, argv);
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(u"QtCraft"_s);
    QGuiApplication::setOrganizationName(u"QtCraft"_s);
    QGuiApplication::setApplicationVersion(QStringLiteral(QTCRAFT_VERSION));
    QcAppOptions options = QcAppOptions::parse(app);
    const bool offscreen = QcOffscreenRender::requested();
    if (options.error.isEmpty() && offscreen) {
        options.error = QcAppOptions::offscreenRunError();
    }
    if (!options.error.isEmpty()) {
        std::println(stderr, "qtcraft: {}", options.error.toStdString());
        return 1;
    }
    // Logging and crash reports from here to the end of main() (decision E-834).
    const QcDiagnosticsSession diagnostics({.program = u"qtcraft"_s,
                                            .dataDirectory = QcDataRoot::path(),
                                            .logByDefault = QcDiagnosticsSettings::storedLogToFile(),
                                            .setup = options.diagnostics,
                                            .arguments = QCoreApplication::arguments()});
    diagnostics.runCrashTest();
    QcAppWindow::prepareScriptedRun(options, offscreen);

    const QString language = QcAppLanguage::uiLanguage(options);
    QcAppWindow::applyAppearance();
    QTranslator translator;
    const QString contentLanguage = QcAppLanguage::install(translator, language);

    QQmlApplicationEngine engine;
    const ImageProviders providers = addImageProviders(engine);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("QtCraft", "Main");
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    auto *game = window ? window->findChild<QcGameController *>(u"game"_s) : nullptr;
    if (!game) {
        qCCritical(lcStartup, "Main.qml must contain a GameController with objectName 'game'");
        return 1;
    }
    QcAppWindow::watchRenderThread(*window);
    const QcResult<std::unique_ptr<QcOffscreenRender>> offscreenRender =
        QcAppWindow::startWindowOrOffscreen(*window, *game, options, offscreen);
    if (!offscreenRender) {
        std::println(stderr, "qtcraft: {}", offscreenRender.error().toStdString());
        return 1;
    }

    game->settings()->diagnostics()->setSession(diagnostics);
    game->setIconProvider(providers.icons);
    game->setMapProvider(providers.maps);
    game->settings()->texturePacks()->setPreviewProvider(providers.texturePackPreviews);
    QcAppStartup::loadSettings(*game);
    QcAppLanguage::followSettings(*game, engine, translator, contentLanguage);
    QcAppStartup::startGame(*game, options, *window, offscreenRender->get());
    game->startHealthLog(diagnostics.healthSeconds());
    return QGuiApplication::exec();
}
