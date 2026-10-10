#include "QcGameSettings.h"
#include "QcTestUtil.h"

#include <QFile>
#include <QTemporaryDir>
#include <QSettings>
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
        settings.debug.level = QcDebugOptions::Level::Full;
        settings.debug.set(QcDebugOptions::Visual::Hitboxes, true);
        settings.debug.set(QcDebugOptions::Visual::RemeshFlash, true);
        settings.showLabels = false;
        settings.language = u"de"_s;
        settings.cameraMode = QcCameraMode::Front;
        settings.appearance.outfit = u"mod:robe"_s;
        settings.appearance.hair.clear();
        settings.appearance.skinColor = 0x010203;
        settings.appearance.hairColor = 0xfffffe;
        settings.appearance.shirtColor = 0x00ff00;
        settings.appearance.pantsColor = 0x000000;
        settings.masterVolume = 0.25;
        settings.effectsVolume = 0.0;
        settings.musicVolume = 1.0;
        settings.ambientVolume = 0.4;
        settings.muted = true;

        const QcStatus saved = settings.save(path);
        QVERIFY2(saved.has_value(), qcErrorText(saved));
        QCOMPARE(QcGameSettings::load(path), settings);
    }

    void audioDefaultsAndBadVolumes()
    {
        const QcGameSettings defaults;
        QCOMPARE(defaults.masterVolume, 0.8);
        QCOMPARE(defaults.effectsVolume, 1.0);
        QCOMPARE(defaults.musicVolume, 0.5);
        QCOMPARE(defaults.ambientVolume, 0.7);
        QVERIFY(!defaults.muted);

        QTemporaryDir dir;
        const QString path = dir.filePath(u"settings.ini"_s);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[audio]\nmaster=1.5\neffects=-2\nmusic=loud\nambient=nan\nmuted=true\n");
        file.close();

        const QcGameSettings settings = QcGameSettings::load(path);
        QCOMPARE(settings.masterVolume, 1.0);
        QCOMPARE(settings.effectsVolume, 0.0);
        QCOMPARE(settings.musicVolume, defaults.musicVolume);
        QCOMPARE(settings.ambientVolume, defaults.ambientVolume);
        QVERIFY(settings.muted);
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
        // An older file with only showDebug=false has the debug display off.
        QCOMPARE(settings.debug.level, QcDebugOptions::Level::Off);
        QCOMPARE(settings.language, QString());
    }

    void debugOptionsAndOlderFiles()
    {
        // The compact display by default, no visualisations.
        const QcGameSettings defaults;
        QCOMPARE(defaults.debug.level, QcDebugOptions::Level::Compact);
        QVERIFY(!defaults.debug.anyVisual());

        QTemporaryDir dir;
        const QString path = dir.filePath(u"settings.ini"_s);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[General]\nshowDebug=true\n[debug]\nmobGoals=true\nlightValues=nonsense\n");
        file.close();
        QcGameSettings loaded = QcGameSettings::load(path);
        QCOMPARE(loaded.debug.level, QcDebugOptions::Level::Compact);
        QVERIFY(loaded.debug.shows(QcDebugOptions::Visual::MobGoals));
        QVERIFY(!loaded.debug.shows(QcDebugOptions::Visual::LightValues));
        QVERIFY(!loaded.debug.shows(QcDebugOptions::Visual::ChunkBorders));

        // Saving writes the level and drops the old key; the level wins over it from then on.
        loaded.debug.level = QcDebugOptions::Level::Full;
        QVERIFY(loaded.save(path).has_value());
        const QSettings stored(path, QSettings::IniFormat);
        QVERIFY(!stored.contains(u"showDebug"_s));
        QCOMPARE(stored.value(u"debug/level"_s).toString(), u"full"_s);
        QCOMPARE(QcGameSettings::load(path).debug, loaded.debug);

        // An unknown level falls back to the default.
        QSettings broken(path, QSettings::IniFormat);
        broken.setValue(u"debug/level"_s, u"loud"_s);
        broken.sync();
        QCOMPARE(QcGameSettings::load(path).debug.level, QcDebugOptions::Level::Compact);
    }

    void debugLevelsCycleAndNamesRoundTrip()
    {
        using L = QcDebugOptions::Level;
        QCOMPARE(QcDebugOptions::nextLevel(L::Off), L::Compact);
        QCOMPARE(QcDebugOptions::nextLevel(L::Compact), L::Full);
        QCOMPARE(QcDebugOptions::nextLevel(L::Full), L::Off);
        for (const L level : {L::Off, L::Compact, L::Full}) {
            QCOMPARE(QcDebugOptions::levelFromName(QcDebugOptions::levelName(level)), std::optional(level));
        }
        for (int visual = 0; visual < QcDebugOptions::visualCount; ++visual) {
            const auto id = QcDebugOptions::visualId(QcDebugOptions::Visual(visual));
            QCOMPARE(QcDebugOptions::visualFromId(id), std::optional(QcDebugOptions::Visual(visual)));
        }
        QCOMPARE(QcDebugOptions::visualFromId(u"xray"_s), std::nullopt);
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
