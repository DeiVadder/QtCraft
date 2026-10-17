#include "app/hooks/QcPackSwitchHitch.h"

#include <QTest>

using namespace Qt::StringLiterals;

namespace {

constexpr qint64 ms = 1'000'000;

} // namespace

// The hitch window of a texture pack switch in a benchmark run (decision E-332).
class TestPackSwitchHitch : public QObject
{
    Q_OBJECT

private slots:
    // The packs switched on come first in their order, the run's packs after them, each once.
    void switchedSelectionPutsThePacksFirst()
    {
        QCOMPARE(QcPackSwitchHitch::selectionWith({}, {u"b:x"_s}), QStringList{u"b:x"_s});
        QCOMPARE(QcPackSwitchHitch::selectionWith({u"a"_s, u"b"_s}, {u"c"_s, u"b"_s}), (QStringList{u"c"_s, u"b"_s, u"a"_s}));
        QCOMPARE(QcPackSwitchHitch::selectionWith({u"a"_s}, {}), QStringList{u"a"_s});
    }

    // Nothing counts before the switch is asked for.
    void framesBeforeTheSwitchAreIgnored()
    {
        QcPackSwitchHitch hitch;
        hitch.addFrame(100 * ms, 300 * ms);
        hitch.addGuiCpu(100 * ms, 40.0);
        QVERIFY(!hitch.started());
        QCOMPARE(hitch.frames(), 0);
        QCOMPARE(hitch.worstFrameMs(), 0.0);
        QCOMPARE(hitch.guiCpuMaxMs(), 0.0);
        QCOMPARE(hitch.atlasMs(), -1.0);

        hitch.start(1000 * ms);
        hitch.addFrame(999 * ms, 300 * ms);
        QCOMPARE(hitch.frames(), 0);
    }

    // From the switch until one second after the atlas: worst frame, slow frames, maxima per frame.
    void watchesUntilOneSecondAfterTheAtlas()
    {
        QcPackSwitchHitch hitch;
        hitch.start(1000 * ms);
        hitch.addFrame(1008 * ms, 8 * ms);
        hitch.addRender(1008 * ms, 1.5, 0.9);
        hitch.atlasReady(1060 * ms);
        QCOMPARE(hitch.atlasMs(), 60.0);
        // A later atlas (a second build) does not move the moment.
        hitch.atlasReady(1500 * ms);
        QCOMPARE(hitch.atlasMs(), 60.0);

        hitch.addFrame(1140 * ms, 80 * ms);
        hitch.addGuiCpu(1140 * ms, 72.5);
        hitch.addFrame(1200 * ms, 50 * ms);
        hitch.addRender(1200 * ms, 30.0, 12.0);
        hitch.addFrame(2060 * ms, 9 * ms);
        QVERIFY(hitch.watching(2060 * ms));
        QVERIFY(!hitch.watching(2061 * ms));
        // After the window: neither counted nor raising a maximum.
        hitch.addFrame(2100 * ms, 500 * ms);
        hitch.addGuiCpu(2100 * ms, 400.0);
        hitch.addRender(2100 * ms, 400.0, 400.0);

        QCOMPARE(hitch.frames(), 4);
        QCOMPARE(hitch.slowFrames(), 2);
        QCOMPARE(hitch.worstFrameMs(), 80.0);
        QCOMPARE(hitch.guiCpuMaxMs(), 72.5);
        QCOMPARE(hitch.renderCpuMaxMs(), 30.0);
        QCOMPARE(hitch.worldRenderMaxMs(), 12.0);
        QCOMPARE(hitch.summary(), u"atlas_ms=60.0 worst_frame_ms=80.00 frames=4 slow_frames=2 gui_cpu_max_ms=72.50 "
                                  u"render_cpu_max_ms=30.00 world_render_max_ms=12.00"_s);
    }

    // Without an atlas (e.g. the selection did not change) the window stays open; a new start clears.
    void withoutAnAtlasTheWindowStaysOpenAndStartClears()
    {
        QcPackSwitchHitch hitch;
        hitch.start(0);
        hitch.addFrame(60'000 * ms, 120 * ms);
        QVERIFY(hitch.watching(60'000 * ms));
        QCOMPARE(hitch.atlasMs(), -1.0);
        QCOMPARE(hitch.slowFrames(), 1);

        hitch.start(70'000 * ms);
        QCOMPARE(hitch.frames(), 0);
        QCOMPARE(hitch.worstFrameMs(), 0.0);
        QCOMPARE(hitch.slowFrames(), 0);
        QVERIFY(!hitch.watching(69'000 * ms));
    }
};

QTEST_GUILESS_MAIN(TestPackSwitchHitch)
#include "test_pack_switch_hitch.moc"
