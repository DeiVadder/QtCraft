#include "QcScreenshotHook.h"
#include "QcSpikeScene.h"
#include "QcWorldView.h"

#include <QGuiApplication>
#include <QLocale>
#include <QTranslator>
#include <QQmlApplicationEngine>
#include <QQuickWindow>

using namespace Qt::StringLiterals;

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(u"QtCraft"_s);
    QGuiApplication::setOrganizationName(u"QtCraft"_s);
    QGuiApplication::setApplicationVersion(QStringLiteral(QTCRAFT_VERSION));

    // QTCRAFT_LANGUAGE overrides the system language, e.g. for screenshots of a translation.
    const QString language = qEnvironmentVariable("QTCRAFT_LANGUAGE");
    const QLocale locale = language.isEmpty() ? QLocale::system() : QLocale(language);
    QTranslator translator;
    if (translator.load(locale, u"qtcraft"_s, u"_"_s, u":/i18n"_s)) {
        QCoreApplication::installTranslator(&translator);
    }

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("QtCraft", "Main");
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (auto *view = window ? window->findChild<QcWorldView *>(u"worldView"_s) : nullptr) {
        QcSpikeScene::populate(view);
    }
    QcScreenshotHook::createFromEnvironment(window, &app);

    return QGuiApplication::exec();
}
