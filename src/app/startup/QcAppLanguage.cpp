#include "app/startup/QcAppLanguage.h"

#include "app/controllers/QcGameController.h"
#include "app/debug/QcAppLogging.h"
#include "app/startup/QcAppOptions.h"

#include <QCoreApplication>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QTranslator>

using namespace Qt::StringLiterals;

QString QcAppLanguage::uiLanguage(const QcAppOptions &options)
{
    QString language = options.language;
    if (language.isEmpty()) {
        language = qEnvironmentVariable("QTCRAFT_LANGUAGE");
    }
    if (language.isEmpty()) {
        language = QcSettingsController::storedLanguage();
    }
    return language;
}

QString QcAppLanguage::install(QTranslator &translator, const QString &name)
{
    const QLocale locale = name.isEmpty() ? QLocale::system() : QLocale(name);
    QCoreApplication::removeTranslator(&translator);
    if (translator.load(locale, u"qtcraft"_s, u"_"_s, u":/i18n"_s)) {
        QCoreApplication::installTranslator(&translator);
        qCInfo(lcStartup).noquote() << u"UI language %1: translation loaded"_s.arg(locale.name());
    } else {
        qCInfo(lcStartup).noquote() << u"UI language %1: no translation, English texts"_s.arg(locale.name());
    }
    return locale.name().left(2);
}

void QcAppLanguage::followSettings(QcGameController &game, QQmlApplicationEngine &engine, QTranslator &translator, const QString &language)
{
    game.setLanguage(language);
    QObject::connect(game.settings(), &QcSettingsController::languageChangeRequested, &engine,
                     [&engine, &translator, &game](const QString &name) {
                         game.setLanguage(install(translator, name));
                         engine.retranslate();
                     });
}
