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
        settings.cameraMode = QcCameraMode::Front;
        settings.appearance.outfit = u"mod:robe"_s;
        settings.appearance.hair.clear();
        settings.appearance.skinColor = 0x010203;
        settings.appearance.hairColor = 0xfffffe;
        settings.appearance.shirtColor = 0x00ff00;
        settings.appearance.pantsColor = 0x000000;
        const QcStatus saved = settings.save(path);
        QVERIFY2(saved.has_value(), qcErrorText(saved));
        QCOMPARE(QcGameSettings::load(path), settings);
    }

    void badCharacterAndViewValuesFallBack()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"settings.ini"_s);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[General]\ncameraMode=sideways\n[character]\nskin=red\nhairColor=#12345\nshirt=#00ff0g\n"
                   "pants=#abcdef\noutfit=");
        file.write(QByteArray(QcAppearance::maxNameLength + 1, 'x'));
        file.write("\n");
        file.close();
        const QcGameSettings settings = QcGameSettings::load(path);
        const QcAppearance defaults;
        QCOMPARE(settings.cameraMode, QcCameraMode::FirstPerson);
        QCOMPARE(settings.appearance.skinColor, defaults.skinColor);
        QCOMPARE(settings.appearance.hairColor, defaults.hairColor);
        QCOMPARE(settings.appearance.shirtColor, defaults.shirtColor);
        QCOMPARE(settings.appearance.pantsColor, 0xabcdefu);
        QCOMPARE(settings.appearance.outfit, defaults.outfit);
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
