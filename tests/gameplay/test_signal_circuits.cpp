#include "common/world/QcLightVolume.h"
#include "QcCircuitFixture.h"
#include "QcTestUtil.h"
#include "server/signal/QcCircuitComponents.h"

#include <QTemporaryDir>

using namespace Qt::StringLiterals;

// Components of Signal Dust on a running server (decisions E-335, E-336, E-339): timings of torches,
// repeaters and lamps, torch burnout on a fast clock, locking, buttons and plates, openers following
// rising and falling power, lever spam and circuits that go on after a restart.
class TestSignalCircuits : public QcCircuitFixture
{
    Q_OBJECT

private:
    // Moves the raw client's player to local (x, y, z) of the area.
    void teleport(int x, int y, int z)
    {
        const QcBlockPos pos = at(x, y, z);
        chat(u"/tp %1 %2 %3"_s.arg(pos.x + 0.5).arg(pos.y).arg(pos.z + 0.5));
        idle(2);
    }

    int updatesAt(QcBlockPos pos) const
    {
        return int(std::ranges::count_if(m_client->blockUpdates, [pos](QcMsg::BlockUpdate update) { return update.pos == pos; }));
    }

    // A lever on the floor at (0, 0, 5), dust east of it, a repeater (delay 1) at x = 4 and a lamp
    // at x = 6 behind more dust.
    void leverRepeaterLamp()
    {
        put(0, 0, 5, u"lever[attach=floor]"_s);
        dustLine(1, 0, 5, 3);
        put(4, 0, 5, u"signal_repeater[facing=east,delay=1]"_s);
        put(5, 0, 5, u"signal_dust"_s);
        put(6, 0, 5, u"signal_lamp"_s);
        ticks(1);
    }

    // A config whose circuits ask for no load tickets (stage 31.3): the edge wait itself is tested.
    static QcServerConfig withoutTickets()
    {
        QcServerConfig config;
        config.limits.chunkTicketsPerTick = 0;
        return config;
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void leverLightsALampThroughDustAndARepeater()
    {
        buildArea();
        leverRepeaterLamp();
        QVERIFY(isBlock(6, 0, 5, "signal_lamp"));

        // The wire carries the signal at once; the repeater passes it on two ticks later.
        flip(0, 0, 5);
        ticks(1);
        QCOMPARE(power(1, 0, 5), 15);
        QCOMPARE(power(3, 0, 5), 13);
        QVERIFY(isBlock(6, 0, 5, "signal_lamp"));
        ticks(1);
        QVERIFY(isBlock(6, 0, 5, "signal_lamp"));
        ticks(1);
        QVERIFY(flag(4, 0, 5, u"powered"_s));
        QCOMPARE(power(5, 0, 5), 15);
        QVERIFY(isBlock(6, 0, 5, "signal_lamp_lit"));
        QCOMPARE(QcLightVolume::blockLightAt(m_server->world(), at(6, 1, 5)), 14);

        // Off: the repeater after its delay, the lamp four ticks later.
        flip(0, 0, 5);
        ticks(3);
        QVERIFY(!flag(4, 0, 5, u"powered"_s));
        ticks(3);
        QVERIFY(isBlock(6, 0, 5, "signal_lamp_lit"));
        ticks(1);
        QVERIFY(isBlock(6, 0, 5, "signal_lamp"));
    }

    void torchInvertsTwoTicksLater()
    {
        buildArea();
        put(5, 0, 5, u"stone"_s);
        put(5, 0, 4, u"lever[attach=south]"_s);
        put(6, 0, 5, u"signal_torch[attach=west]"_s);
        ticks(3);
        QVERIFY(isBlock(6, 0, 5, "signal_torch"));

        flip(5, 0, 4);
        ticks(2);
        QVERIFY(isBlock(6, 0, 5, "signal_torch"));
        ticks(1);
        QVERIFY(isBlock(6, 0, 5, "signal_torch_off"));

        flip(5, 0, 4);
        ticks(2);
        QVERIFY(isBlock(6, 0, 5, "signal_torch_off"));
        ticks(1);
        QVERIFY(isBlock(6, 0, 5, "signal_torch"));
    }

    void fastClockBurnsTheTorchOut()
    {
        // A torch on a block whose wire runs back into that block: it switches every two ticks.
        buildArea();
        put(5, 0, 5, u"stone"_s);
        put(6, 0, 5, u"stone"_s);
        for (const auto &[x, y, z] : {std::tuple{6, 1, 5}, {7, 0, 5}, {7, 0, 6}, {7, 0, 7}, {6, 0, 7}, {5, 0, 7}, {5, 0, 6}}) {
            put(x, y, z, u"signal_dust"_s);
        }
        put(5, 1, 5, u"signal_torch[attach=floor]"_s);

        QList<int> offTicks;
        bool lit = true;
        for (int tick = 1; tick <= 400; ++tick) {
            ticks(1);
            const bool now = isBlock(5, 1, 5, "signal_torch");
            if (lit && !now) {
                offTicks.append(tick);
            }
            lit = now;
            if (offTicks.size() == QcCircuitComponents::burnoutToggles + 1) {
                break;
            }
        }

        // Eight times out within 60 ticks, then dark for 160 ticks, then the clock starts again.
        QVERIFY2(offTicks.size() == QcCircuitComponents::burnoutToggles + 1, qPrintable(QString::number(offTicks.size())));
        const int burnt = offTicks[QcCircuitComponents::burnoutToggles - 1];
        QVERIFY(burnt - offTicks.constFirst() < QcCircuitComponents::burnoutWindowTicks);
        for (int i = 1; i < QcCircuitComponents::burnoutToggles; ++i) {
            QCOMPARE(offTicks[i] - offTicks[i - 1], 4);
        }
        QVERIFY(offTicks.constLast() - burnt >= QcCircuitComponents::burnoutTicks);
        QVERIFY(offTicks.constLast() - burnt <= QcCircuitComponents::burnoutTicks + 8);
    }

    void repeaterStretchesAShortPulse()
    {
        buildArea();
        put(0, 0, 5, u"lever[attach=floor]"_s);
        put(1, 0, 5, u"signal_repeater[facing=east,delay=4]"_s);
        put(2, 0, 5, u"signal_dust"_s);
        ticks(1);

        // On for one tick only: the repeater's output still lasts its full delay of eight ticks.
        flip(0, 0, 5);
        ticks(1);
        flip(0, 0, 5);
        int on = 0;
        for (int tick = 0; tick < 30; ++tick) {
            ticks(1);
            on += power(2, 0, 5) > 0 ? 1 : 0;
        }
        QCOMPARE(on, 4 * QcCircuitComponents::repeaterStepTicks);
    }

    void poweredRepeaterLocksItsNeighbour()
    {
        buildArea();
        // A repeater east with its input lever; a second repeater points north into its side.
        put(0, 0, 5, u"lever[attach=floor]"_s);
        put(1, 0, 5, u"signal_repeater[facing=east,delay=1]"_s);
        put(1, 0, 7, u"lever[attach=floor]"_s);
        put(1, 0, 6, u"signal_repeater[facing=north,delay=1]"_s);
        ticks(1);

        flip(1, 0, 7);
        ticks(4);
        QVERIFY(flag(1, 0, 5, u"locked"_s));
        flip(0, 0, 5);
        ticks(6);
        QVERIFY(!flag(1, 0, 5, u"powered"_s));

        // Unlocked, it takes its input.
        flip(1, 0, 7);
        ticks(6);
        QVERIFY(!flag(1, 0, 5, u"locked"_s));
        QVERIFY(flag(1, 0, 5, u"powered"_s));
    }

    void buttonsSpringBack()
    {
        buildArea();
        put(2, 0, 2, u"stone_button[attach=floor]"_s);
        put(6, 0, 2, u"oak_button[attach=floor]"_s);
        ticks(1);
        flip(2, 0, 2);
        flip(6, 0, 2);

        int stone = 0;
        int oak = 0;
        for (int tick = 0; tick < 40; ++tick) {
            stone += flag(2, 0, 2, u"powered"_s) ? 1 : 0;
            oak += flag(6, 0, 2, u"powered"_s) ? 1 : 0;
            ticks(1);
        }
        QCOMPARE(stone, 20);
        QCOMPARE(oak, 30);
    }

    void platesSeeWhatStandsOnThem()
    {
        buildArea();
        put(3, 0, 3, u"oak_pressure_plate"_s);
        put(8, 0, 3, u"stone_pressure_plate"_s);
        put(3, 0, 4, u"signal_lamp"_s);
        ticks(1);

        // An item presses the wooden plate but not the stone one.
        for (const QcBlockPos cell : {at(3, 0, 3), at(8, 0, 3)}) {
            const QcVec3 centre{cell.x + 0.5, cell.y + 0.05, cell.z + 0.5};
            const quint32 dropped = m_server->items().spawnScattered({item("qtcraft:stick"), 1, 0}, centre, 900000 + quint32(cell.x & 0xFFFF));
            for (QcItemEntity &entity : m_server->items().editableItems()) {
                if (entity.id == dropped) {
                    entity.motion.position = centre;
                    entity.motion.velocity = {};
                }
            }
        }
        ticks(1);
        QVERIFY(flag(3, 0, 3, u"powered"_s));
        QVERIFY(isBlock(3, 0, 4, "signal_lamp_lit"));
        QVERIFY(!flag(8, 0, 3, u"powered"_s));

        // A player presses the stone plate.
        teleport(8, 0, 3);
        QVERIFY(flag(8, 0, 3, u"powered"_s));

        // Gone from the plate: it springs back within its check interval.
        m_server->items().removeWhere([](const QcItemEntity &) { return true; });
        teleport(11, 0, 11);
        ticks(QcCircuitComponents::plateCheckTicks + 1);
        QVERIFY(!flag(3, 0, 3, u"powered"_s));
        QVERIFY(!flag(8, 0, 3, u"powered"_s));
    }

    void openersFollowRisingAndFallingPower()
    {
        buildArea();
        put(3, 0, 8, u"oak_door[facing=north,half=bottom]"_s);
        put(3, 1, 8, u"oak_door[facing=north,half=top]"_s);
        put(2, 0, 8, u"lever[attach=floor]"_s);
        put(6, 0, 8, u"oak_trapdoor[facing=north]"_s);
        put(7, 0, 8, u"lever[attach=floor]"_s);
        ticks(1);

        flip(2, 0, 8);
        flip(7, 0, 8);
        ticks(1);
        QVERIFY(flag(3, 0, 8, u"open"_s));
        QVERIFY(flag(3, 1, 8, u"open"_s));
        QVERIFY(flag(6, 0, 8, u"open"_s));

        // Closed by hand while powered, it stays closed until the power falls and rises again.
        teleport(3, 0, 10);
        send(QcMsg::UseItem{at(3, 0, 8), QcBlockFace::South});
        QVERIFY(!flag(3, 0, 8, u"open"_s));
        ticks(3);
        QVERIFY(!flag(3, 0, 8, u"open"_s));
        flip(2, 0, 8);
        ticks(1);
        QVERIFY(!flag(3, 0, 8, u"open"_s));
        flip(2, 0, 8);
        ticks(1);
        QVERIFY(flag(3, 0, 8, u"open"_s));
    }

    void lampPoweredAgainWithinItsDelayStaysLit()
    {
        buildArea();
        put(4, 0, 4, u"lever[attach=floor]"_s);
        put(5, 0, 4, u"signal_lamp"_s);
        ticks(1);
        flip(4, 0, 4);
        ticks(1);
        flip(4, 0, 4);
        ticks(2);
        flip(4, 0, 4);
        for (int tick = 0; tick < 10; ++tick) {
            ticks(1);
            QVERIFY2(isBlock(5, 0, 4, "signal_lamp_lit"), qPrintable(QString::number(tick)));
        }
    }

    void burntOutTorchStaysOffWhenItsBlockIsPowered()
    {
        buildArea();
        // The clock of fastClockBurnsTheTorchOut with a lever on its block.
        put(5, 0, 5, u"stone"_s);
        put(6, 0, 5, u"stone"_s);
        for (const auto &[x, y, z] : {std::tuple{6, 1, 5}, {7, 0, 5}, {7, 0, 6}, {7, 0, 7}, {6, 0, 7}, {5, 0, 7}, {5, 0, 6}}) {
            put(x, y, z, u"signal_dust"_s);
        }
        put(4, 0, 5, u"lever[attach=east]"_s);
        put(5, 1, 5, u"signal_torch[attach=floor]"_s);
        for (int tick = 0; tick < 60 && !burntOut(at(5, 1, 5)); ++tick) {
            ticks(1);
        }
        QVERIFY(burntOut(at(5, 1, 5)));

        // Burnt out with its block powered by the lever: when the burnout ends it stays off.
        flip(4, 0, 5);
        ticks(QcCircuitComponents::burnoutTicks + 10);
        QVERIFY(isBlock(5, 1, 5, "signal_torch_off"));
        QVERIFY(!burntOut(at(5, 1, 5)));
    }

    void openersOfEveryKind()
    {
        buildArea();
        // An iron door opens by signal only; a fence gate opens; a door powered at its top half opens.
        put(2, 0, 3, u"iron_door[facing=north,half=bottom]"_s);
        put(2, 1, 3, u"iron_door[facing=north,half=top]"_s);
        put(1, 0, 3, u"lever[attach=floor]"_s);
        put(6, 0, 3, u"oak_fence_gate[facing=north]"_s);
        put(5, 0, 3, u"lever[attach=floor]"_s);
        put(10, 0, 3, u"oak_door[facing=north,half=bottom]"_s);
        put(10, 1, 3, u"oak_door[facing=north,half=top]"_s);
        put(9, 1, 3, u"stone"_s);
        put(9, 1, 2, u"lever[attach=south]"_s);
        ticks(1);

        teleport(2, 0, 5);
        send(QcMsg::UseItem{at(2, 0, 3), QcBlockFace::South});
        QVERIFY(!flag(2, 0, 3, u"open"_s));

        flip(1, 0, 3);
        flip(5, 0, 3);
        flip(9, 1, 2);
        ticks(1);
        QVERIFY(flag(2, 0, 3, u"open"_s));
        QVERIFY(flag(2, 1, 3, u"open"_s));
        QVERIFY(flag(6, 0, 3, u"open"_s));
        QVERIFY(flag(10, 0, 3, u"open"_s));
        QVERIFY(flag(10, 1, 3, u"open"_s));
    }

    void repeaterDelaySpamIsBounded()
    {
        buildArea();
        put(5, 0, 5, u"signal_repeater[facing=east,delay=1]"_s);
        ticks(1);
        teleport(5, 0, 7);
        for (int i = 0; i < 15; ++i) {
            m_client->connection.send(QcMsg::UseItem{at(5, 0, 5), QcBlockFace::Up});
        }
        QcTestUtil::pump();
        ticks(1);
        // Ten of the fifteen clicks pass: 1 -> 2 -> 3 -> 4 -> 1 ... ten steps end at 3.
        QCOMPARE(property(5, 0, 5, u"delay"_s), u"3"_s);
    }

    void newDoorWhereTheLeverIsOnOpens()
    {
        buildArea();
        put(3, 0, 8, u"oak_door[facing=north,half=bottom]"_s);
        put(3, 1, 8, u"oak_door[facing=north,half=top]"_s);
        put(2, 0, 8, u"lever[attach=floor,powered=true]"_s);
        ticks(1);
        QVERIFY(flag(3, 0, 8, u"open"_s));

        // Replaced while the lever stays on: the new door is powered from the start and opens.
        put(3, 1, 8, u"air"_s);
        put(3, 0, 8, u"air"_s);
        ticks(1);
        QVERIFY(!poweredOpener(at(3, 0, 8)));
        put(3, 0, 8, u"oak_door[facing=north,half=bottom]"_s);
        put(3, 1, 8, u"oak_door[facing=north,half=top]"_s);
        ticks(1);
        QVERIFY(flag(3, 0, 8, u"open"_s));
    }

    void floodedChangesCarryOverAndScheduledTicksKeepTheirTime()
    {
        buildArea();
        // Twelve lamps next to levers that are on, set without telling the circuits: dark.
        for (int x = 0; x < 12; ++x) {
            QVERIFY(m_server->world().setBlock(at(x, 0, 2), state(u"signal_lamp"_s)));
            QVERIFY(m_server->world().setBlock(at(x, 0, 1), state(u"lever[attach=floor,powered=true]"_s)));
        }
        put(5, 0, 8, u"stone"_s);
        put(5, 0, 7, u"lever[attach=south]"_s);
        put(6, 0, 8, u"signal_torch[attach=west]"_s);
        ticks(3);

        // A torch is due to switch two ticks after its lever; then a flood of changes far beyond one
        // tick's budget arrives. The flood is carried over, the torch still switches on time.
        flip(5, 0, 7);
        ticks(1);
        for (int i = 0; i < 40000; ++i) {
            m_server->circuitState().changes.append({at(i % 12, 0, 2), qcAirBlockId, qcAirBlockId});
        }
        ticks(1);
        QVERIFY2(m_server->systems().circuits.lastTickWork() <= 2 * QcServerLimits{}.signalBudget,
                 qPrintable(QString::number(m_server->systems().circuits.lastTickWork())));
        QVERIFY(!m_server->circuitState().changes.isEmpty());
        QVERIFY(isBlock(6, 0, 8, "signal_torch"));
        ticks(1);
        QVERIFY(isBlock(6, 0, 8, "signal_torch_off"));
        QVERIFY(!m_server->circuitState().changes.isEmpty());

        for (int tick = 0; tick < 20 && !m_server->circuitState().changes.isEmpty(); ++tick) {
            ticks(1);
        }
        QVERIFY(m_server->circuitState().changes.isEmpty());
        for (int x = 0; x < 12; ++x) {
            QVERIFY(isBlock(x, 0, 2, "signal_lamp_lit"));
        }
    }

    void circuitWaitsForAnUnloadedNeighbourChunk()
    {
        buildArea(withoutTickets());
        // Far from the player, in chunks that only the test loads: lever and dust in one chunk, more
        // dust and a lamp across the border in the next.
        clearArea(m_origin.offset(4000, 0, 4000));
        int border = 1;
        while (at(border, 0, 5).chunk() == at(border - 1, 0, 5).chunk()) {
            ++border;
        }
        put(border - 3, 0, 5, u"lever[attach=floor]"_s);
        dustLine(border - 2, 0, 5, 3);
        put(border + 1, 0, 5, u"signal_lamp"_s);
        ticks(2);
        const QString before = look(border - 1, 0, 5);

        // The far chunk goes away; the near dust waits instead of reading it as air.
        const QcChunkPos far = at(border, 0, 5).chunk();
        std::shared_ptr<QcChunk> taken = m_server->world().takeChunk(far);
        QVERIFY(taken);
        flip(border - 3, 0, 5);
        ticks(3);
        QCOMPARE(look(border - 1, 0, 5), before);

        // Back again: the wire goes on to the lamp.
        m_server->world().adoptStoredChunks({{far, taken}});
        ticks(3);
        QCOMPARE(power(border - 1, 0, 5), 14);
        QVERIFY(isBlock(border + 1, 0, 5, "signal_lamp_lit"));
    }

    void wireFarFromAnUnloadedChunkDoesNotWait()
    {
        buildArea(withoutTickets());
        // The chunk border 12 blocks east of the area's origin.
        const QcBlockPos far = m_origin.offset(4000, 0, 4000);
        clearArea(far.offset(((4 - far.x) % 16 + 16) % 16, 0, 0));
        const int border = 12;
        QVERIFY(at(border, 0, 5).chunk() != at(border - 1, 0, 5).chunk());
        // A wire at the border and another one far from it, switched in the same step.
        put(border - 3, 0, 5, u"lever[attach=floor]"_s);
        dustLine(border - 2, 0, 5, 3);
        put(border - 12, 0, 9, u"lever[attach=floor]"_s);
        dustLine(border - 11, 0, 9, 3);
        ticks(2);
        std::shared_ptr<QcChunk> taken = m_server->world().takeChunk(at(border, 0, 5).chunk());
        QVERIFY(taken);
        flip(border - 3, 0, 5);
        flip(border - 12, 0, 9);
        ticks(3);
        QCOMPARE(power(border - 1, 0, 5), 0);
        QCOMPARE(power(border - 9, 0, 9), 13);
        m_server->world().adoptStoredChunks({{at(border, 0, 5).chunk(), taken}});
        ticks(3);
        QCOMPARE(power(border - 1, 0, 5), 14);
    }

    void leverSpamIsBounded()
    {
        buildArea();
        put(5, 0, 5, u"lever[attach=floor]"_s);
        ticks(1);
        teleport(5, 0, 7);
        const QcBlockPos lever = at(5, 0, 5);
        const int before = updatesAt(lever);

        // Fifteen clicks at once: the switch budget lets the first ten through.
        for (int i = 0; i < 15; ++i) {
            m_client->connection.send(QcMsg::UseItem{lever, QcBlockFace::Up});
        }
        QcTestUtil::pump();
        ticks(1);
        QCOMPARE(updatesAt(lever) - before, int(QcRateLimiter::limit(QcRateLimiter::Kind::Switch).burst));
    }

    void circuitsGoOnAfterARestart()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        buildArea(config);
        leverRepeaterLamp();
        flip(0, 0, 5);
        ticks(1);

        // Saved while the repeater waits for its tick: nothing else would wake it after loading,
        // yet it comes (joining runs two ticks).
        QCOMPARE(m_server->blockEntities().schedule().size(), 1);
        QVERIFY(m_server->save().has_value());
        const QcBlockPos origin = m_origin;
        cleanup();
        join(config);
        m_origin = origin;
        ticks(1);
        QVERIFY(flag(4, 0, 5, u"powered"_s));
        QVERIFY(isBlock(6, 0, 5, "signal_lamp_lit"));
    }
};

QTEST_GUILESS_MAIN(TestSignalCircuits)
#include "test_signal_circuits.moc"
