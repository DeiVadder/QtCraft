#include "clientcore/preview/QcFramePacer.h"
#include "clientcore/settings/QcDisplayOptions.h"

#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

using Mode = QcDisplayOptions::WindowMode;

namespace {

constexpr qint64 msNs = 1'000'000;

// Frames of a display at `hz` for one second, asked of `pacer`: how many it lets through.
int framesInASecond(QcFramePacer &pacer, double hz)
{
    int drawn = 0;
    const auto vsyncNs = static_cast<qint64>(1e9 / hz);
    for (qint64 now = 0; now < 1'000'000'000; now += vsyncNs) {
        drawn += pacer.frameDue(now) ? 1 : 0;
    }
    return drawn;
}

} // namespace

// The display options (decision E-835): stored values and their checks, the limit in force, the
// fullscreen key's mode and the window's place; and the frame pacer on a fake clock.
class TestDisplayOptions : public QObject
{
    Q_OBJECT

private:
    static void writeFile(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }

private slots:
    void defaultsAreTodaysBehaviour()
    {
        const QcDisplayOptions options;
        QCOMPARE(options.windowMode, Mode::Windowed);
        QVERIFY(options.vsync);
        QCOMPARE(options.frameLimit, 0);
        QCOMPARE(options.backgroundFrameLimit, 30);
        QTemporaryDir dir;
        QCOMPARE(QcDisplayOptions::load(dir.filePath(u"missing.ini"_s)), options);
    }

    void roundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"settings.ini"_s);
        QcDisplayOptions options;
        options.windowMode = Mode::Fullscreen;
        options.previousMode = Mode::Borderless;
        options.screen = u"DELL U2720Q"_s;
        options.windowGeometry = QRect(100, 80, 1600, 900);
        options.vsync = false;
        options.frameLimit = 144;
        options.backgroundFrameLimit = 0;
        {
            QSettings file(path, QSettings::IniFormat);
            options.save(file);
        }
        QCOMPARE(QcDisplayOptions::load(path), options);

        // The keys a player or a guide may name.
        const QSettings file(path, QSettings::IniFormat);
        QCOMPARE(file.value(u"display/windowMode"_s).toString(), u"fullscreen"_s);
        QCOMPARE(file.value(u"display/frameLimit"_s).toInt(), 144);
        QCOMPARE(file.value(u"display/backgroundFrameLimit"_s).toInt(), 0);
        QCOMPARE(file.value(u"display/vsync"_s).toBool(), false);
    }

    // Unknown modes and limits that are none of the choices give the defaults, with a warning.
    void invalidValuesFallBackWithAWarning()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"settings.ini"_s);
        writeFile(path, "[display]\nwindowMode=exclusive\npreviousMode=fullscreen\nframeLimit=75\nbackgroundFrameLimit=abc\n");
        QTest::ignoreMessage(QtWarningMsg, "Unknown window mode 'exclusive' in display/windowMode; using windowed");
        QTest::ignoreMessage(QtWarningMsg, "Unsupported frame limit '75' in display/frameLimit; using 0");
        QTest::ignoreMessage(QtWarningMsg, "Unsupported frame limit 'abc' in display/backgroundFrameLimit; using 30");
        const QcDisplayOptions options = QcDisplayOptions::load(path);
        QCOMPARE(options.windowMode, Mode::Windowed);
        // Fullscreen cannot be the mode fullscreen returns to.
        QCOMPARE(options.previousMode, Mode::Windowed);
        QCOMPARE(options.frameLimit, 0);
        QCOMPARE(options.backgroundFrameLimit, 30);
        QCOMPARE(QcDisplayOptions::modeFromName(u"Fullscreen"), std::nullopt);
        QCOMPARE(QcDisplayOptions::modeFromName(u"borderless"), std::optional(Mode::Borderless));
        QVERIFY(QcDisplayOptions::isFrameLimit(90));
        QVERIFY(!QcDisplayOptions::isFrameLimit(-1));
        QVERIFY(!QcDisplayOptions::isFrameLimit(240));
    }

    void activeLimitInMenusAndBackground_data()
    {
        QTest::addColumn<int>("game");
        QTest::addColumn<int>("background");
        QTest::addColumn<int>("foreground");
        QTest::addColumn<int>("behind");
        QTest::newRow("defaults") << 0 << 30 << 0 << 30;
        QTest::newRow("game limit below") << 30 << 60 << 30 << 30;
        QTest::newRow("both") << 144 << 60 << 144 << 60;
        QTest::newRow("no background limit") << 120 << 0 << 120 << 120;
        QTest::newRow("none") << 0 << 0 << 0 << 0;
    }
    void activeLimitInMenusAndBackground()
    {
        QFETCH(int, game);
        QFETCH(int, background);
        QFETCH(int, foreground);
        QFETCH(int, behind);
        QcDisplayOptions options;
        options.frameLimit = game;
        options.backgroundFrameLimit = background;
        QCOMPARE(options.activeFrameLimit(true), foreground);
        QCOMPARE(options.activeFrameLimit(false), behind);
    }

    void fullscreenKeyReturnsToThePreviousMode()
    {
        QcDisplayOptions options;
        QCOMPARE(options.toggledFullscreen(), Mode::Fullscreen);
        options.windowMode = Mode::Borderless;
        QCOMPARE(options.toggledFullscreen(), Mode::Fullscreen);
        options.windowMode = Mode::Fullscreen;
        options.previousMode = Mode::Borderless;
        QCOMPARE(options.toggledFullscreen(), Mode::Borderless);
        options.previousMode = Mode::Fullscreen;
        QCOMPARE(options.toggledFullscreen(), Mode::Windowed);
    }

    void screensByName()
    {
        const QStringList names{u"Built-in Retina Display"_s, u"DELL U2720Q"_s};
        QCOMPARE(QcDisplayOptions::screenIndex(names, u"DELL U2720Q"_s), std::optional<qsizetype>(1));
        QCOMPARE(QcDisplayOptions::screenIndex(names, u"LG 27GN950"_s), std::nullopt);
        QCOMPARE(QcDisplayOptions::screenIndex(names, QString()), std::nullopt);
    }

    // The stored frame comes back while its centre is on the screen (moved and shrunk to fit);
    // a frame of a screen that is gone is replaced by the default size, centred.
    void windowedFrameStaysOnTheScreen()
    {
        const QRect screen(0, 25, 1920, 1055);
        const QSize defaultSize(1280, 720);
        QCOMPARE(QcDisplayOptions::windowedFrame(QRect(100, 100, 1600, 900), screen, defaultSize), QRect(100, 100, 1600, 900));
        QCOMPARE(QcDisplayOptions::windowedFrame(QRect(1000, 600, 1280, 720), screen, defaultSize), QRect(640, 360, 1280, 720));
        QCOMPARE(QcDisplayOptions::windowedFrame(QRect(-10, 0, 3000, 2000), screen, defaultSize), screen);
        QCOMPARE(QcDisplayOptions::windowedFrame(QRect(2500, 100, 1280, 720), screen, defaultSize), QRect(320, 193, 1280, 720));
        QCOMPARE(QcDisplayOptions::windowedFrame(QRect(), screen, defaultSize), QRect(320, 193, 1280, 720));

        // With a title bar of 28 and a border of 1 the whole window stays inside: a client area at
        // the top of the screen moves down below the title bar, one as large as the screen shrinks.
        const QMargins frame(1, 28, 1, 1);
        QCOMPARE(QcDisplayOptions::windowedFrame(QRect(100, 25, 1280, 720), screen, defaultSize, frame), QRect(100, 53, 1280, 720));
        QCOMPARE(QcDisplayOptions::windowedFrame(QRect(-10, 0, 3000, 2000), screen, defaultSize, frame), QRect(1, 53, 1918, 1026));
        QCOMPARE(QcDisplayOptions::windowedFrame(QRect(1000, 600, 1280, 720), screen, defaultSize, frame), QRect(639, 359, 1280, 720));
        // The default size is centred in the area inside the frame.
        QCOMPARE(QcDisplayOptions::windowedFrame(QRect(), screen, defaultSize, frame), QRect(320, 206, 1280, 720));
    }

    void unlimitedPacingPassesEveryFrame()
    {
        QcFramePacer pacer;
        QCOMPARE(pacer.limit(), 0);
        QCOMPARE(pacer.intervalNs(), 0);
        QCOMPARE(framesInASecond(pacer, 250.0), 250);
        QCOMPARE(pacer.delayNs(5 * msNs), 0);
        pacer.setLimit(-5);
        QCOMPARE(pacer.limit(), 0);
    }

    void limitIsRespected_data()
    {
        QTest::addColumn<int>("limit");
        QTest::addColumn<double>("hz");
        QTest::newRow("60 on 120 Hz") << 60 << 120.0;
        QTest::newRow("60 on 144 Hz") << 60 << 144.0;
        QTest::newRow("30 on 60 Hz") << 30 << 60.0;
        QTest::newRow("90 on 1000 Hz (no VSync)") << 90 << 1000.0;
        QTest::newRow("144 on 1000 Hz") << 144 << 1000.0;
        QTest::newRow("144 on 144 Hz") << 144 << 144.0;
    }
    void limitIsRespected()
    {
        QFETCH(int, limit);
        QFETCH(double, hz);
        QcFramePacer pacer;
        pacer.setLimit(limit);
        const int drawn = framesInASecond(pacer, hz);
        // Never above the limit (one frame of rounding at the second's edge), and close to it where
        // the display allows that many: frames a little late stay on the grid.
        QVERIFY2(drawn <= limit + 1, qPrintable(QString::number(drawn)));
        QVERIFY2(drawn >= std::min(double(limit), hz) * 0.9, qPrintable(QString::number(drawn)));
    }

    void delayUntilTheNextFrame()
    {
        QcFramePacer pacer;
        pacer.setLimit(60);
        QCOMPARE(pacer.delayNs(0), 0);
        QVERIFY(pacer.frameDue(0));
        // The next frame is due 16.7 ms later; a millisecond early still counts (timers fire early).
        QCOMPARE(pacer.delayNs(0), 16'666'666 - msNs);
        QVERIFY(!pacer.frameDue(8 * msNs));
        QCOMPARE(pacer.delayNs(8 * msNs), 16'666'666 - msNs - 8 * msNs);
        QVERIFY(pacer.frameDue(16 * msNs));
        // A frame 5 ms late keeps the grid (next at 50 ms), a long pause starts a new one.
        QVERIFY(pacer.frameDue(38 * msNs));
        QCOMPARE(pacer.delayNs(38 * msNs), 50 * msNs - 2 - msNs - 38 * msNs);
        QVERIFY(pacer.frameDue(1000 * msNs));
        QCOMPARE(pacer.delayNs(1000 * msNs), 16'666'666 - msNs);
        QCOMPARE(pacer.delayNs(2000 * msNs), 0);
    }

    // The limit for menus and an unfocused window comes in force at once: a frame waiting for the
    // old limit is not held back.
    void changingTheLimitStartsANewGrid()
    {
        QcFramePacer pacer;
        pacer.setLimit(30);
        QVERIFY(pacer.frameDue(0));
        QVERIFY(!pacer.frameDue(10 * msNs));
        pacer.setLimit(144);
        QVERIFY(pacer.frameDue(10 * msNs));
        QVERIFY(!pacer.frameDue(12 * msNs));
        QVERIFY(pacer.frameDue(17 * msNs));
        pacer.setLimit(144);
        QVERIFY(!pacer.frameDue(18 * msNs));
        pacer.setLimit(0);
        QVERIFY(pacer.frameDue(18 * msNs));
        QVERIFY(pacer.frameDue(18 * msNs));
    }
};

QTEST_GUILESS_MAIN(TestDisplayOptions)
#include "test_display_options.moc"
