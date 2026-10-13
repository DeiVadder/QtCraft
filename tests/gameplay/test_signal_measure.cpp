#include "QcServerTestFixture.h"

#include <QElapsedTimer>

#include <algorithm>

using namespace Qt::StringLiterals;

// Headless measurement of the signal phase (decision E-335): work and milliseconds per server tick
// for a long dust line toggled every tick, a dust field, many repeater clocks and many pistons. The
// sizes are small by default (a quick check that the scenarios work); QTCRAFT_SIGNAL_MEASURE=full
// runs the sizes of the performance record (4096 dust, 64 x 64, 1000 clocks, 500 pistons), meant for
// a Release build. Only the behaviour is checked, never the times.
class TestSignalMeasure : public QcServerTestFixture
{
    Q_OBJECT

private:
    QcBlockPos m_base;
    bool m_full = qEnvironmentVariable("QTCRAFT_SIGNAL_MEASURE") == u"full"_s;

    QcBlockId state(const QString &text) const { return *m_content->stateId(u"qtcraft:"_s + text); }

    // Sets a block like a player's change; the chunk stays loaded (it is changed).
    void put(int x, int z, const QString &text, int y = 0)
    {
        const QcBlockPos pos = m_base.offset(x, y, z);
        m_server->world().loadChunk(pos.chunk());
        m_server->world().setBlock(pos.offset(0, -1, 0), state(u"stone"_s));
        const QcBlockId before = m_server->world().blockAt(pos).value_or(qcAirBlockId);
        m_server->world().setBlock(pos, state(text));
        m_server->circuitState().noteChange(m_server->world(), pos, before, state(text));
    }

    // Loads the chunks of a box (with a margin) and keeps them loaded.
    void prepare(int sizeX, int sizeZ)
    {
        for (int x = -16; x <= sizeX + 16; x += 8) {
            for (int z = -16; z <= sizeZ + 16; z += 8) {
                const QcBlockPos pos = m_base.offset(x, -2, z);
                m_server->world().loadChunk(pos.chunk());
                m_server->world().setBlock(pos, state(u"stone"_s));
            }
        }
    }

    struct Sample
    {
        double meanMs = 0.0;
        double maxMs = 0.0;
        double meanWork = 0.0;
        qsizetype maxWork = 0;
    };

    // Runs `count` ticks, calling `before` ahead of each, and measures the whole tick.
    template<typename Before>
    Sample measure(int count, Before before)
    {
        Sample sample;
        QElapsedTimer timer;
        for (int tick = 0; tick < count; ++tick) {
            before(tick);
            timer.start();
            m_server->tick();
            const double ms = double(timer.nsecsElapsed()) / 1e6;
            pump();
            const qsizetype work = m_server->circuits().lastTickWork();
            sample.meanMs += ms / count;
            sample.maxMs = std::max(sample.maxMs, ms);
            sample.meanWork += double(work) / count;
            sample.maxWork = std::max(sample.maxWork, work);
        }
        return sample;
    }

    static void report(const char *scenario, int size, const Sample &sample)
    {
        qInfo("SIGNAL-MEASURE %s size=%d tickMeanMs=%.3f tickMaxMs=%.3f workMean=%.0f workMax=%lld", scenario, size, sample.meanMs,
              sample.maxMs, sample.meanWork, qint64(sample.maxWork));
    }

    void toggleLever(QcBlockPos lever) { QVERIFY(m_server->circuits().use(lever)); }

    // Ticks until the changes of building it are worked off.
    void settleBuild()
    {
        for (int tick = 0; tick < 400 && !m_server->circuitState().changes.isEmpty(); ++tick) {
            ticks(1);
        }
        QVERIFY(m_server->circuitState().changes.isEmpty());
        ticks(3);
    }

private slots:
    void init()
    {
        join();
        chat(u"/gamerule mobSpawning false"_s);
        m_base = m_feet.offset(3000, 40, 3000);
    }

    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void longDustLineToggledEveryTick()
    {
        const int length = m_full ? 4096 : 256;
        prepare(length, 1);
        put(0, 0, u"lever[attach=floor]"_s);
        for (int x = 1; x <= length; ++x) {
            put(x, 0, u"signal_dust"_s);
        }
        settleBuild();

        const QcBlockPos lever = m_base;
        const Sample sample = measure(40, [&](int) { toggleLever(lever); });
        report("line", length, sample);
        QVERIFY(m_content->property(*m_server->world().blockAt(m_base.offset(length, 0, 0)), u"power"_s) == u"0"_s);
    }

    void dustFieldToggledEveryTick()
    {
        const int side = m_full ? 64 : 16;
        prepare(side + 1, side);
        put(0, 0, u"lever[attach=floor]"_s);
        for (int x = 1; x <= side; ++x) {
            for (int z = 0; z < side; ++z) {
                put(x, z, u"signal_dust"_s);
            }
        }
        settleBuild();

        const QcBlockPos lever = m_base;
        const Sample sample = measure(40, [&](int) { toggleLever(lever); });
        report("field", side, sample);
    }

    void repeaterClocks()
    {
        // A torch on a block, its wire through a repeater (delay 4) back into the block: a clock of 20
        // ticks per period, slow enough not to burn out.
        const int clocks = m_full ? 1000 : 32;
        const int columns = int(std::ceil(std::sqrt(double(clocks))));
        prepare(columns * 4, columns * 4);
        for (int i = 0; i < clocks; ++i) {
            const int x = (i % columns) * 4;
            const int z = (i / columns) * 4;
            put(x, z, u"stone"_s);
            put(x + 1, z, u"signal_torch[attach=west]"_s);
            put(x + 1, z + 1, u"signal_dust"_s);
            put(x + 1, z + 2, u"signal_dust"_s);
            put(x, z + 2, u"signal_dust"_s);
            put(x, z + 1, u"signal_repeater[facing=north,delay=4]"_s);
        }
        settleBuild();
        ticks(40);

        int switches = 0;
        const QcBlockPos firstTorch = m_base.offset(1, 0, 0);
        QcBlockId last = *m_server->world().blockAt(firstTorch);
        const Sample sample = measure(100, [&](int) {
            const QcBlockId now = *m_server->world().blockAt(firstTorch);
            switches += now != last ? 1 : 0;
            last = now;
        });
        report("clocks", clocks, sample);
        QVERIFY2(switches >= 6, qPrintable(QString::number(switches)));
        QVERIFY(m_server->circuitState().burntOutUntil.isEmpty());
    }

    void pistonsSwitchedTogether()
    {
        const int pistons = m_full ? 500 : 32;
        const int columns = int(std::ceil(std::sqrt(double(pistons))));
        prepare(columns * 4, columns * 3);
        QList<QcBlockPos> levers;
        for (int i = 0; i < pistons; ++i) {
            const int x = (i % columns) * 4;
            const int z = (i / columns) * 3;
            put(x, z, u"lever[attach=floor]"_s);
            put(x + 1, z, u"sticky_signal_piston[facing=east]"_s);
            put(x + 2, z, u"stone"_s);
            levers.append(m_base.offset(x, 0, z));
        }
        settleBuild();

        const Sample sample = measure(40, [&](int tick) {
            if (tick % 5 == 0) {
                for (const QcBlockPos &lever : std::as_const(levers)) {
                    toggleLever(lever);
                }
            }
        });
        report("pistons", pistons, sample);
        QCOMPARE(m_content->property(*m_server->world().blockAt(m_base.offset(1, 0, 0)), u"extended"_s), u"false"_s);
    }
};

QTEST_GUILESS_MAIN(TestSignalMeasure)
#include "test_signal_measure.moc"
