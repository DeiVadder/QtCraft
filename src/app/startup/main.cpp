#include "app/controllers/QcGameController.h"
#include "app/hooks/QcOffscreenRender.h"
#include "app/models/QcItemIconProvider.h"
#include "app/models/QcTexturePackPreviewProvider.h"
#include "app/startup/QcAppLanguage.h"
#include "app/startup/QcAppOptions.h"
#include "app/startup/QcAppStartup.h"
#include "app/startup/QcAppWindow.h"

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

// The steps of the start in their order: command line, scripted-run setup, language, the QML
// engine and its main window, then settings, language switching and the game (decision E-530).
int main(int argc, char *argv[])
{
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
    QcAppWindow::prepareScriptedRun(options, offscreen);

    const QString language = QcAppLanguage::uiLanguage(options);
    QcAppWindow::applyColorScheme();
    QTranslator translator;
    const QString contentLanguage = QcAppLanguage::install(translator, language);

    QQmlApplicationEngine engine;
    // The engine owns and deletes the provider.
    auto *icons = new QcItemIconProvider;
    engine.addImageProvider(QString::fromLatin1(QcItemIconProvider::providerId), icons);
    auto *texturePackPreviews = new QcTexturePackPreviewProvider;
    engine.addImageProvider(QString::fromLatin1(QcTexturePackPreviewProvider::providerId), texturePackPreviews);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("QtCraft", "Main");
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    auto *game = window ? window->findChild<QcGameController *>(u"game"_s) : nullptr;
    if (!game) {
        qCritical("Main.qml must contain a GameController with objectName 'game'");
        return 1;
    }
    const QcResult<std::unique_ptr<QcOffscreenRender>> offscreenRender =
        QcAppWindow::startWindowOrOffscreen(*window, *game, options, offscreen);
    if (!offscreenRender) {
        std::println(stderr, "qtcraft: {}", offscreenRender.error().toStdString());
        return 1;
    }

    game->setIconProvider(icons);
    game->settings()->texturePacks()->setPreviewProvider(texturePackPreviews);
    QcAppStartup::loadSettings(*game);
    QcAppLanguage::followSettings(*game, engine, translator, contentLanguage);
    QcAppStartup::startGame(*game, options, *window, offscreenRender->get());
    return QGuiApplication::exec();
}
