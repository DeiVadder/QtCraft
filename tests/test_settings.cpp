#include "QcGameSettings.h"
#include "QcTestUtil.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <limits>

using namespace Qt::StringLiterals;

class TestSettings : public QObject
{
    Q_OBJECT

private slots:
    void missingFileGivesDefaults()
    {
        QTemporaryDir dir;
        QCOMPARE(QcGameSettings::load(dir.filePath(u"settings.ini"_s)), QcGameSettings{});
    }

    void settingsRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"nested/settings.ini"_s);
        QcGameSettings settings;
        settings.viewDistance = 12;
        settings.fieldOfView = 90.0;
        settings.mouseSensitivity = 0.3;
        settings.invertMouse = true;
        settings.showDebug = false;
        settings.language = u"de"_s;
        const QcStatus saved = settings.save(path);
        QVERIFY2(saved.has_value(), qcErrorText(saved));
        QCOMPARE(QcGameSettings::load(path), settings);
    }

    void valuesFromTheFileAreClamped()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"settings.ini"_s);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[General]\nviewDistance=99\nfieldOfView=500\nmouseSensitivity=nan\ninvertMouse=true\n"
                   "showDebug=false\nlanguage=../../evil\n");
        file.close();
        const QcGameSettings settings = QcGameSettings::load(path);
        QCOMPARE(settings.viewDistance, QcGameSettings::maxViewDistance);
        QCOMPARE(settings.fieldOfView, QcGameSettings::maxFieldOfView);
        QCOMPARE(settings.mouseSensitivity, QcGameSettings::defaultMouseSensitivity);
        QVERIFY(settings.invertMouse);
        QVERIFY(!settings.showDebug);
        QCOMPARE(settings.language, QString());
    }

    void clampHelpers()
    {
        QCOMPARE(QcGameSettings::clampViewDistance(0), QcGameSettings::minViewDistance);
        QCOMPARE(QcGameSettings::clampFieldOfView(std::numeric_limits<double>::infinity()), QcGameSettings::defaultFieldOfView);
        QCOMPARE(QcGameSettings::clampMouseSensitivity(-1.0), QcGameSettings::minMouseSensitivity);
        QCOMPARE(QcGameSettings::cleanLanguage(u"de"_s), u"de"_s);
        QCOMPARE(QcGameSettings::cleanLanguage(u"DE"_s), QString());
        QCOMPARE(QcGameSettings::cleanLanguage(QString()), QString());
    }
};

QTEST_GUILESS_MAIN(TestSettings)
#include "test_settings.moc"
