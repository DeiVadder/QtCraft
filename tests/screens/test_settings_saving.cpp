// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcWorldLibraryController.h"
#include "app/session/QcSessionParts.h"
#include "app/settings/QcAudioSettings.h"
#include "app/settings/QcCharacterSettings.h"
#include "app/settings/QcSettingsController.h"
#include "audio/QcAudioEngine.h"
#include "clientcore/meshing/QcMeshScheduler.h"
#include "clientcore/session/QcClientSession.h"
#include "clientcore/settings/QcGameSettings.h"
#include "worlds/QcLocalServerHost.h"

#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;

// The settings store themselves when they change (79.59): saveDelayMs after the last change, at
// once when the session ends, the game quits or the controller ends. No screen has to call
// anything, so a settings screen destroyed while open (a disconnect, leaving the world) loses
// nothing; loading and the run's scene options store nothing.
class TestSettingsSaving : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_data.isValid());
        qputenv("QTCRAFT_DATA_DIR", m_data.path().toUtf8());
    }

    void init() { QFile::remove(QcSettingsController::settingsPath()); }

    // A change is stored without any close(), once the value rests.
    void changeIsStoredWhenItRests()
    {
        Fixture fixture;
        fixture.settings.setMouseSensitivity(0.5);
        QVERIFY(fixture.settings.savePending());
        QVERIFY(!QFile::exists(QcSettingsController::settingsPath()));
        QTRY_VERIFY(!fixture.settings.savePending());
        QCOMPARE(stored().mouseSensitivity, 0.5);
    }

    // A slider's many changes are written once, after the last one.
    void burstIsStoredOnce()
    {
        Fixture fixture;
        for (int degrees = 60; degrees <= 100; ++degrees) {
            fixture.settings.setFieldOfView(degrees);
            QCoreApplication::processEvents();
        }
        QVERIFY(fixture.settings.savePending());
        QVERIFY(!QFile::exists(QcSettingsController::settingsPath()));
        QTRY_VERIFY(!fixture.settings.savePending());
        QCOMPARE(stored().fieldOfView, 100.0);
    }

    // The controller ends (the window closes) before the change rested: stored anyway, and the
    // next start loads it.
    void endStoresAWaitingChange()
    {
        {
            Fixture fixture;
            fixture.settings.setInvertMouse(true);
            fixture.settings.audio()->setMusicVolume(0.25);
            QVERIFY(fixture.settings.savePending());
        }
        Fixture again;
        QVERIFY(again.settings.invertMouse());
        QCOMPARE(again.settings.audio()->musicVolume(), 0.25);
    }

    // Where the music comes from (decision E-1137): the tracks by default; the switch to
    // generated music reaches the music at once, is stored and comes back with the next start.
    void musicSourceSwitchesAndIsStored()
    {
        {
            Fixture fixture;
            QCOMPARE(fixture.settings.audio()->musicSource(), QcAudioSettings::MusicSource::Tracks);
            QCOMPARE(fixture.audio.music().director().source(), QcMusicSource::Tracks);
            fixture.settings.audio()->setMusicSource(QcAudioSettings::MusicSource::Generated);
            QCOMPARE(fixture.audio.music().director().source(), QcMusicSource::Generated);
            QVERIFY(fixture.settings.savePending());
        }
        QCOMPARE(stored().audio.musicSource, QcMusicSource::Generated);
        Fixture again;
        QCOMPARE(again.settings.audio()->musicSource(), QcAudioSettings::MusicSource::Generated);
        QCOMPARE(again.audio.music().director().source(), QcMusicSource::Generated);

        // A value from outside the choices is refused with a warning.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Ignoring unknown music source 7"_s));
        again.settings.audio()->setMusicSource(static_cast<QcAudioSettings::MusicSource>(7));
        QCOMPARE(again.settings.audio()->musicSource(), QcAudioSettings::MusicSource::Generated);
    }

    // The session's end and the game's quit store a waiting change at once.
    void sessionEndAndQuitStoreAtOnce()
    {
        Fixture fixture;
        fixture.settings.setShowLabels(false);
        fixture.settings.flushPendingSave();
        QVERIFY(!fixture.settings.savePending());
        QCOMPARE(stored().showLabels, false);

        fixture.settings.setCameraShake(false);
        QVERIFY(QMetaObject::invokeMethod(QCoreApplication::instance(), "aboutToQuit"));
        QVERIFY(!fixture.settings.savePending());
        QCOMPARE(stored().cameraShake, false);
    }

    // Loading stores nothing, and a change back to the stored value writes nothing.
    void loadingAndNoChangeStoreNothing()
    {
        Fixture fixture;
        QVERIFY(!fixture.settings.savePending());
        const double sensitivity = fixture.settings.mouseSensitivity();
        fixture.settings.setMouseSensitivity(0.7);
        fixture.settings.setMouseSensitivity(sensitivity);
        fixture.settings.flushPendingSave();
        QVERIFY(!QFile::exists(QcSettingsController::settingsPath()));
    }

    // The scene options of the command line are used, not stored: unrelated signals (a draft
    // opened and closed) write nothing, and a later change by the player stores the stored values
    // in their place; changing the option itself stores the player's choice.
    void sceneOptionsStayUnsaved()
    {
        Fixture fixture;
        QcSettingsController &settings = fixture.settings;
        const int storedDistance = settings.viewDistance();
        settings.overrideViewDistance(storedDistance + 2);
        settings.setCameraModeValue(QcCameraMode::Side);
        settings.debug()->setLevel(static_cast<int>(QcDebugOptions::Level::Full));
        settings.debug()->setShown(QcDebugOptions::Visual::Hitboxes, true);
        settings.keepSceneOptionsUnsaved();
        QVERIFY(!settings.savePending());

        settings.character()->beginDraft();
        settings.character()->endDraft();
        QVERIFY(settings.savePending());
        settings.flushPendingSave();
        QVERIFY(!QFile::exists(QcSettingsController::settingsPath()));

        settings.setInvertMouse(true);
        settings.flushPendingSave();
        QcGameSettings written = stored();
        QVERIFY(written.invertMouse);
        QCOMPARE(written.viewDistance, storedDistance);
        QCOMPARE(settings.viewDistance(), storedDistance + 2);
        QCOMPARE(written.cameraMode, QcGameSettings().cameraMode);
        QCOMPARE(written.debug, QcDebugOptions());

        settings.setCameraModeValue(QcCameraMode::Behind);
        settings.flushPendingSave();
        written = stored();
        QCOMPARE(written.cameraMode, QcCameraMode::Behind);
        QCOMPARE(written.debug, QcDebugOptions());
    }

    // The player picks a scene option's value himself after changing it (79.76): his choice is
    // stored, not the value stored before the run.
    void sceneValuePickedAgainIsStored()
    {
        Fixture fixture;
        QcSettingsController &settings = fixture.settings;
        const int storedDistance = settings.viewDistance();
        const int sceneDistance = storedDistance + 2;
        settings.overrideViewDistance(sceneDistance);
        settings.setCameraModeValue(QcCameraMode::Side);
        settings.keepSceneOptionsUnsaved();

        settings.setViewDistance(sceneDistance + 1);
        settings.setViewDistance(sceneDistance);
        settings.setCameraModeValue(QcCameraMode::Behind);
        settings.setCameraModeValue(QcCameraMode::Side);
        settings.flushPendingSave();
        const QcGameSettings written = stored();
        QCOMPARE(written.viewDistance, sceneDistance);
        QCOMPARE(written.cameraMode, QcCameraMode::Side);
    }

    // Each debug option of the scene is held on its own (79.76): a switched visualisation stores
    // that switch only, not the scene's level or its other visualisations; the level is stored
    // once the player changes it.
    void debugSwitchStoresOnlyThatSwitch()
    {
        Fixture fixture;
        QcSettingsController &settings = fixture.settings;
        QcDebugSettings &debug = *settings.debug();
        debug.setLevel(static_cast<int>(QcDebugOptions::Level::Full));
        debug.setShown(QcDebugOptions::Visual::Hitboxes, true);
        settings.keepSceneOptionsUnsaved();

        debug.setShown(QcDebugOptions::Visual::ChunkBorders, true);
        settings.flushPendingSave();
        QcGameSettings written = stored();
        QCOMPARE(written.debug.level, QcDebugOptions().level);
        QVERIFY(!written.debug.shows(QcDebugOptions::Visual::Hitboxes));
        QVERIFY(written.debug.shows(QcDebugOptions::Visual::ChunkBorders));

        debug.setLevel(static_cast<int>(QcDebugOptions::Level::Off));
        debug.setLevel(static_cast<int>(QcDebugOptions::Level::Full));
        debug.setShown(QcDebugOptions::Visual::Hitboxes, false);
        debug.setShown(QcDebugOptions::Visual::Hitboxes, true);
        settings.flushPendingSave();
        written = stored();
        QCOMPARE(written.debug.level, QcDebugOptions::Level::Full);
        QVERIFY(written.debug.shows(QcDebugOptions::Visual::Hitboxes));
    }

    // A saved draft of the look is stored by the settings; a draft left open is not.
    void savedDraftIsStored()
    {
        {
            Fixture fixture;
            QcCharacterSettings &character = *fixture.settings.character();
            character.beginDraft();
            character.setPart(u"skin"_s, u"#224466"_s);
            character.saveDraft();
            character.setPart(u"shirt"_s, u"#665544"_s);
            QVERIFY(fixture.settings.savePending());
        }
        const QcGameSettings settings = stored();
        QCOMPARE(settings.appearance.skinColor, 0x224466u);
        QVERIFY(settings.appearance.shirtColor != 0x665544u);
    }

    // Older settings files store the interface size as auto: loaded as 100 %, the step the
    // screen offers.
    void autoUiScaleLoadsAsOneHundred()
    {
        QcGameSettings old;
        old.uiScale = QcUiScale::Step::Auto;
        QVERIFY(old.save(QcSettingsController::settingsPath()));
        Fixture fixture;
        QCOMPARE(fixture.settings.uiScale(), QcUiScaling::Step::Percent100);
        QVERIFY(!fixture.settings.savePending());
    }

private:
    struct Fixture
    {
        Fixture() { settings.loadSettings(); }

        QcSessionParts parts;
        QcAudioEngine audio;
        QcWorldLibraryController library;
        QcSettingsController settings{parts, library, &audio};
    };

    static QcGameSettings stored() { return QcGameSettings::load(QcSettingsController::settingsPath()); }

    QTemporaryDir m_data;
};

QTEST_MAIN(TestSettingsSaving)
#include "test_settings_saving.moc"
