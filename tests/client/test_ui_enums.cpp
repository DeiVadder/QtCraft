#include "app/controllers/QcPhotoMode.h"
#include "app/controllers/QcScreenState.h"
#include "app/controllers/QcSettingsController.h"
#include "app/controllers/QcWorldLibraryController.h"
#include "app/preview/QcCharacterPreview.h"
#include "app/session/QcSessionParts.h"
#include "audio/QcAudioEngine.h"
#include "clientcore/meshing/QcMeshScheduler.h"
#include "clientcore/session/QcClientSession.h"
#include "worlds/QcLocalServerHost.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLocale>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

// The enumerations QML sees instead of strings (GameController.Overlay, GameSettings.CameraMode,
// CharacterPreview.Animation), the warnings of rejected values, and the photo mode's state and
// file names (decisions E-692, E-693).
class TestUiEnums : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_data.isValid());
        qputenv("QTCRAFT_DATA_DIR", m_data.path().toUtf8());
    }

    // Every overlay has a command-line name and comes back from it; unknown names do not.
    void overlayNamesRoundTrip()
    {
        const QMetaEnum overlays = QMetaEnum::fromType<QcScreenState::Overlay>();
        for (int index = 0; index < overlays.keyCount(); ++index) {
            const auto overlay = static_cast<QcScreenState::Overlay>(overlays.value(index));
            QCOMPARE(QcScreenState::overlayFromName(QcScreenState::overlayName(overlay)), overlay);
        }
        QCOMPARE(QcScreenState::overlayFromName(u""_s), QcScreenState::Overlay::None);
        QCOMPARE(QcScreenState::overlayFromName(u"texturepacks"_s), QcScreenState::Overlay::TexturePacks);
        QCOMPARE(QcScreenState::overlayFromName(u"Pause"_s), std::nullopt);
        QCOMPARE(QcScreenState::overlayFromName(u"handbook"_s), std::nullopt);
        QCOMPARE(QcScreenState::overlayFromName(u"display"_s), QcScreenState::Overlay::Display);
        QCOMPARE(QcScreenState::overlayFromName(u"audio"_s), QcScreenState::Overlay::Audio);
        QCOMPARE(QcScreenState::overlayFromName(u"interface"_s), QcScreenState::Overlay::Interface);
    }

    // The pause menu and its screens count as menus (background frame limit, decision E-835); the
    // screens over the running game do not.
    void menusOfThePause()
    {
        using O = QcScreenState::Overlay;
        for (const O overlay : {O::Pause, O::Settings, O::Display, O::Audio, O::Interface, O::Controls, O::Gamepad, O::Debug, O::TexturePacks, O::Character, O::Lan}) {
            QVERIFY2(QcScreenState::isMenu(overlay), qPrintable(QcScreenState::overlayName(overlay)));
        }
        for (const O overlay : {O::None, O::Inventory, O::Creative, O::Furnace, O::Chest, O::Trade, O::Chat, O::Sign, O::Stonecutter}) {
            QVERIFY2(!QcScreenState::isMenu(overlay), qPrintable(QcScreenState::overlayName(overlay)));
        }
    }

    // An overlay value outside the enumeration (QML passes ints) is refused with a warning; the
    // same overlay again changes nothing and stays silent.
    void overlaySetterRefusesUnknownValues()
    {
        QcSessionParts parts;
        QcScreenState screens(parts);
        QSignalSpy changed(&screens, &QcScreenState::overlayChanged);

        screens.setOverlay(QcScreenState::Overlay::Pause);
        QCOMPARE(changed.count(), 1);
        screens.setOverlay(QcScreenState::Overlay::Pause);
        QCOMPARE(changed.count(), 1);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Ignoring unknown overlay 99"_s));
        screens.setOverlay(static_cast<QcScreenState::Overlay>(99));
        QCOMPARE(screens.overlay(), QcScreenState::Overlay::Pause);
        QCOMPARE(changed.count(), 1);
    }

    // The camera mode is an enumeration property; unknown values and out-of-range numbers warn.
    void cameraModeIsAnEnum()
    {
        QcSessionParts parts;
        QcAudioEngine audio;
        QcWorldLibraryController library;
        QcSettingsController settings(parts, library, &audio);
        const QMetaProperty property = settings.metaObject()->property(settings.metaObject()->indexOfProperty("cameraMode"));
        QVERIFY(property.isEnumType());

        QVERIFY(settings.setProperty("cameraMode", int(QcSettingsController::CameraMode::Behind)));
        QCOMPARE(settings.cameraModeValue(), QcCameraMode::Behind);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Ignoring unknown camera mode 7"_s));
        settings.setCameraMode(static_cast<QcSettingsController::CameraMode>(7));
        QCOMPARE(settings.cameraModeValue(), QcCameraMode::Behind);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"fieldOfView.*instead of 500"_s));
        settings.setFieldOfView(500.0);
    }

    // The UI scale (decision E-725) is an enumeration property; an unknown step warns and changes
    // nothing, the same step again is silent; the choice is saved with the settings and read back.
    void uiScaleIsAnEnumAndPersists()
    {
        QcSessionParts parts;
        QcAudioEngine audio;
        QcWorldLibraryController library;
        {
            QcSettingsController settings(parts, library, &audio);
            const QMetaProperty property = settings.metaObject()->property(settings.metaObject()->indexOfProperty("uiScale"));
            QVERIFY(property.isEnumType());
            QCOMPARE(settings.uiScale(), QcUiScaling::Step::Percent100);
            QSignalSpy changed(&settings, &QcSettingsController::settingsChanged);

            QVERIFY(settings.setProperty("uiScale", static_cast<int>(QcUiScaling::Step::Percent150)));
            QCOMPARE(settings.uiScale(), QcUiScaling::Step::Percent150);
            QCOMPARE(changed.count(), 1);
            settings.setUiScale(QcUiScaling::Step::Percent150);
            QCOMPARE(changed.count(), 1);

            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Ignoring unknown UI scale 9"_s));
            settings.setUiScale(static_cast<QcUiScaling::Step>(9));
            QCOMPARE(settings.uiScale(), QcUiScaling::Step::Percent150);

            settings.setUiScale(QcUiScaling::Step::Percent175);
            settings.saveSettings();
        }

        QcSettingsController loaded(parts, library, &audio);
        loaded.loadSettings();
        QCOMPARE(loaded.uiScale(), QcUiScaling::Step::Percent175);
    }

    // Texture filtering (decision E-721) is an enumeration property whose values are the sample
    // counts; an unknown value warns and changes nothing, the choice is saved and read back.
    void textureFilteringIsAnEnumAndPersists()
    {
        QcSessionParts parts;
        QcAudioEngine audio;
        QcWorldLibraryController library;
        using Filtering = QcSettingsController::TextureFiltering;
        {
            QcSettingsController settings(parts, library, &audio);
            const QMetaProperty property = settings.metaObject()->property(settings.metaObject()->indexOfProperty("textureFiltering"));
            QVERIFY(property.isEnumType());
            QCOMPARE(settings.textureFiltering(), Filtering::X4);
            QCOMPARE(settings.textureFilteringSamples(), 4);
            QSignalSpy changed(&settings, &QcSettingsController::settingsChanged);

            QVERIFY(settings.setProperty("textureFiltering", int(Filtering::Off)));
            QCOMPARE(settings.textureFilteringSamples(), 1);
            QCOMPARE(changed.count(), 1);
            settings.setTextureFiltering(Filtering::Off);
            QCOMPARE(changed.count(), 1);

            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Ignoring unknown texture filtering 3"_s));
            settings.setTextureFiltering(Filtering(3));
            QCOMPARE(settings.textureFiltering(), Filtering::Off);

            settings.setTextureFiltering(Filtering::X8);
            settings.saveSettings();
        }

        QcSettingsController loaded(parts, library, &audio);
        loaded.loadSettings();
        QCOMPARE(loaded.textureFiltering(), Filtering::X8);
        QCOMPARE(loaded.textureFilteringSamples(), 8);
    }

    // The preview's animation is an enumeration property in QcPreviewPose's order.
    void previewAnimationIsAnEnum()
    {
        QcCharacterPreview preview;
        const QMetaProperty property = preview.metaObject()->property(preview.metaObject()->indexOfProperty("animation"));
        QVERIFY(property.isEnumType());
        QCOMPARE(preview.animation(), QcCharacterPreview::Animation::Stand);

        QSignalSpy changed(&preview, &QcCharacterPreview::animationChanged);
        QVERIFY(preview.setProperty("animation", int(QcCharacterPreview::Animation::Mine)));
        QCOMPARE(preview.animation(), QcCharacterPreview::Animation::Mine);
        QCOMPARE(changed.count(), 1);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"ignoring unknown animation 5"_s));
        preview.setAnimation(static_cast<QcCharacterPreview::Animation>(5));
        QCOMPARE(preview.animation(), QcCharacterPreview::Animation::Mine);
    }

    // Hiding the HUD hides the hand as the setting says; showHud() ends it.
    void photoModeHidesHudAndHand()
    {
        QcSessionParts parts;
        QcAudioEngine audio;
        QcWorldLibraryController library;
        QcSettingsController settings(parts, library, &audio);
        QcPhotoMode photo(settings);
        QVERIFY(!photo.hudHidden());

        photo.toggleHud();
        QVERIFY(photo.hudHidden());
        QVERIFY(photo.handHidden());
        settings.setPhotoHidesHand(false);
        QVERIFY(!photo.handHidden());

        photo.showHud();
        QVERIFY(!photo.hudHidden());
    }

    // Screenshots go to <data>/screenshots under the time, numbered when the name is taken.
    void screenshotsAreSavedWithTimeNames()
    {
        QcSessionParts parts;
        QcAudioEngine audio;
        QcWorldLibraryController library;
        QcSettingsController settings(parts, library, &audio);
        QcPhotoMode photo(settings);
        QVERIFY(!photo.takeScreenshot());

        QImage picture(8, 4, QImage::Format_RGB32);
        picture.fill(Qt::red);
        // The notices bind to capturing, so the grab never shows the previous one.
        bool capturedWhileCapturing = false;
        photo.setGrabber([picture, &photo, &capturedWhileCapturing] {
            capturedWhileCapturing = photo.capturing();
            return picture;
        });
        QSignalSpy capturing(&photo, &QcPhotoMode::capturingChanged);
        const QcResult<QString> first = photo.takeScreenshot();
        QVERIFY(first);
        QVERIFY(capturedWhileCapturing);
        QVERIFY(!photo.capturing());
        QCOMPARE(capturing.count(), 2);
        QCOMPARE(QFileInfo(*first).dir().absolutePath(), QDir(m_data.path()).filePath(u"screenshots"_s));
        QCOMPARE(QImage(*first).size(), QSize(8, 4));
        QCOMPARE(photo.lastScreenshot(), QFileInfo(*first).fileName());

        const QDateTime time(QDate(2026, 10, 19), QTime(14, 3, 7));
        const QString folder = QcPhotoMode::directory();
        QCOMPARE(QcPhotoMode::fileName(time, folder), u"2026-10-19_14.03.07.png"_s);
        QVERIFY(picture.save(QDir(folder).filePath(u"2026-10-19_14.03.07.png"_s)));
        QCOMPARE(QcPhotoMode::fileName(time, folder), u"2026-10-19_14.03.07_2.png"_s);
    }

    // A screenshot that cannot be written says so in a translated notice and changes nothing.
    void failedScreenshotIsAnnounced()
    {
        QcSessionParts parts;
        QcAudioEngine audio;
        QcWorldLibraryController library;
        QcSettingsController settings(parts, library, &audio);
        QcPhotoMode photo(settings);
        QImage picture(8, 4, QImage::Format_RGB32);
        picture.fill(Qt::red);
        photo.setGrabber([picture] { return picture; });
        const QString folder = QcPhotoMode::directory();
        QVERIFY(QDir(folder).removeRecursively());
        QFile blocker(folder);
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();

        QSignalSpy failed(&photo, &QcPhotoMode::screenshotFailed);
        QSignalSpy saved(&photo, &QcPhotoMode::screenshotSaved);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Cannot save a screenshot in"_s));
        const QcResult<QString> result = photo.takeScreenshot();
        QVERIFY(!result);
        QCOMPARE(failed.count(), 1);
        QCOMPARE(saved.count(), 0);
        QCOMPARE(failed.first().first().toString(), result.error());
        QVERIFY2(result.error().startsWith(u"Screenshot not saved"_s), qPrintable(result.error()));
        QVERIFY(photo.lastScreenshot().isEmpty());
        QVERIFY(QFile::remove(folder));
    }

    // The main menu shows when a world was played (decision E-852): today and yesterday with the
    // time, earlier days as the short date; the day changes at local midnight.
    void worldListSaysWhenAWorldWasPlayed()
    {
        const QDateTime now(QDate(2026, 10, 22), QTime(0, 1));
        const QDateTime lastMinute(QDate(2026, 10, 21), QTime(23, 59));
        const QString time = QLocale().toString(QTime(0, 0), QLocale::ShortFormat);
        QCOMPARE(QcWorldLibraryController::playedText(QDateTime(QDate(2026, 10, 22), QTime(0, 0)), now), u"Today, %1"_s.arg(time));
        QCOMPARE(QcWorldLibraryController::playedText(lastMinute, now),
                 u"Yesterday, %1"_s.arg(QLocale().toString(QTime(23, 59), QLocale::ShortFormat)));
        QCOMPARE(QcWorldLibraryController::playedText(QDateTime(QDate(2026, 10, 20), QTime(23, 59)), now),
                 QLocale().toString(QDate(2026, 10, 20), QLocale::ShortFormat));
        // In UTC, too: the menu reads world.json's times as they are stored.
        QCOMPARE(QcWorldLibraryController::playedText(lastMinute.toUTC(), now), QcWorldLibraryController::playedText(lastMinute, now));
    }

private:
    QTemporaryDir m_data;
};

QTEST_MAIN(TestUiEnums)
#include "test_ui_enums.moc"
