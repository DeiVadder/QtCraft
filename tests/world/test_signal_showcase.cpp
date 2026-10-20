#include "common/physics/QcPhysics.h"
#include "QcServerTestFixture.h"
#include "QcTestUtil.h"
#include "server/blocks/QcRails.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worldgen/showcase/QcShowcaseGenerator.h"

#include <QTemporaryDir>

using namespace Qt::StringLiterals;

// The Signal Dust station of the showcase world (decision E-340): its demos start by themselves
// once their chunks are loaded around a player, and each shows what it is about after some ticks:
// lit lamps, an opened door, dust shapes, a burnt-out clock, a waiting and an updated BUD piston, a
// spat block, a closed and an open piston door, a pushed chest with its contents and armour stand,
// a long wire; the rails station of stage 32.2 (a cart kept going by powered rails lights a lamp at
// a detector rail, carts unloaded into chests by hoppers under the rails); the ice station of stage
// 54 (a cart kept going by powered rails jumps a gap off a ramp and lands on the track again, round
// after round; a cart rolls off its track onto blue ice and coasts far); the tracks of the stations
// with carts end at a buffer stop except where a cart is meant to fly off (decision E-430).
class TestSignalShowcase : public QcServerTestFixture
{
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    QcShowcasePlan m_plan;

    // The area of the demo; a demo the plan does not have fails the test case (an empty area comes back).
    QcShowcaseAreaInfo area(const QString &demo) const
    {
        for (const QcShowcaseAreaInfo &candidate : m_plan.areas) {
            if (candidate.name == demo) {
                return candidate;
            }
        }
        QcTestUtil::failCase(QStringLiteral("no demo %1").arg(demo));
        return {};
    }

    QcBlockPos origin(const QString &demo) const { return area(demo).origin; }

    QString block(const QString &demo, int x, int y, int z) const
    {
        const std::optional<QcBlockId> id = m_server->world().blockAt(origin(demo).offset(x, y, z));
        return id ? m_content->block(*id).name.section(u':', 1) : u"unloaded"_s;
    }

    QString property(const QString &demo, int x, int y, int z, const QString &name) const
    {
        const std::optional<QcBlockId> id = m_server->world().blockAt(origin(demo).offset(x, y, z));
        return id ? m_content->property(*id, name) : u"unloaded"_s;
    }

    // Takes the player to the middle of a demo; its chunks load around him and its circuits start.
    void visit(const QString &demo)
    {
        const QcShowcaseAreaInfo info = area(demo);
        const QcBlockPos middle = info.origin.offset(info.sizeX / 2, 1, info.sizeZ / 2);
        chat(u"/tp %1 %2 %3"_s.arg(middle.x).arg(middle.y + 6).arg(middle.z));
        for (int dx = -2; dx <= 2; ++dx) {
            for (int dz = -2; dz <= 2; ++dz) {
                m_server->world().loadChunk({middle.chunk().x + dx, middle.chunk().z + dz});
            }
        }
        idle(60);
    }

    // Wheat in the farm's chest.
    int harvested(const QString &demo) const
    {
        const QcBlockEntities::Container *chest = m_server->blockEntities().container(origin(demo).offset(10, 1, 2));
        int wheat = 0;
        for (const QcItemStack &stack : chest ? chest->stacks : QList<QcItemStack>()) {
            wheat += !stack.isEmpty() && m_content->item(stack.item).name == u"qtcraft:wheat"_s ? stack.count : 0;
        }
        return wheat;
    }

    // Young wheat on the field.
    int planted(const QString &demo) const
    {
        int young = 0;
        for (int x = 3; x < 8; ++x) {
            young += block(demo, x, 2, 2) == u"wheat_crop"_s && property(demo, x, 2, 2, u"age"_s) == u"0"_s ? 1 : 0;
        }
        return young;
    }

    // Runs one flush: the water comes and goes again.
    bool flushes(const QString &demo)
    {
        bool poured = false;
        for (int tick = 0; tick < 200 && !poured; ++tick) {
            idle(1);
            poured = block(demo, 2, 2, 2) == u"water"_s;
        }
        idle(600);
        return poured && block(demo, 2, 2, 2) == u"air"_s;
    }

    // The ends of the tracks in a demo with no rail going on and no block level with them (a cart flies
    // off there), as "x y z side" in the demo's coordinates.
    QStringList openTrackEnds(const QString &demo)
    {
        static constexpr std::array<const char *, 6> sideNames{"east", "west", "up", "down", "south", "north"};
        const QcShowcaseAreaInfo info = area(demo);
        for (int x = 0; x < info.sizeX; x += QcWorld::chunkSize) {
            for (int z = 0; z < info.sizeZ; z += QcWorld::chunkSize) {
                m_server->world().loadChunk(info.origin.offset(x, 0, z).chunk());
            }
            m_server->world().loadChunk(info.origin.offset(x, 0, info.sizeZ - 1).chunk());
        }
        m_server->world().loadChunk(info.origin.offset(info.sizeX - 1, 0, info.sizeZ - 1).chunk());

        const QcRails::Table &rails = m_server->carts().rails();
        QStringList open;
        for (int x = -1; x <= info.sizeX; ++x) {
            for (int y = 0; y < info.sizeY; ++y) {
                for (int z = -1; z <= info.sizeZ; ++z) {
                    const QcBlockPos cell = info.origin.offset(x, y, z);
                    const std::optional<QcRails::Track> &track = rails.trackOf(m_server->world().blockAt(cell).value_or(qcAirBlockId));
                    for (int end = 0; track && end < 2; ++end) {
                        const QcRails::End &at = track->ends[std::size_t(end)];
                        const QcBlockPos beyond = qcNeighbour(cell, at.side).offset(0, at.raised ? 1 : 0, 0);
                        if (!QcRails::joined(m_server->world(), rails, cell, *track, end)
                            && QcPhysics::collisionBoxesAt(m_server->world(), beyond).isEmpty()) {
                            open.append(u"%1 %2 %3 %4"_s.arg(x).arg(y).arg(z).arg(QLatin1StringView(sideNames[std::size_t(at.side)])));
                        }
                    }
                }
            }
        }
        return open;
    }

private slots:
    void stationTracksEndAtBufferStops()
    {
        // The ice station's ramp, off whose top the loop cart jumps, and the track that sends a
        // cart onto the blue ice are open on purpose.
        QCOMPARE(openTrackEnds(u"demo-rails"_s), QStringList());
        QCOMPARE(openTrackEnds(u"demo-rails_signal"_s), QStringList());
        QCOMPARE(openTrackEnds(u"demo-ice_slide"_s), (QStringList{u"4 1 9 east"_s, u"14 1 4 west"_s}));
    }

    void initTestCase()
    {
        // A world directory, so the showcase's setup runs; the player sees a whole demo.
        QVERIFY(m_directory.isValid());
        QcServerConfig config;
        config.generator = u"showcase"_s;
        config.worldDirectory = m_directory.path();
        config.autosaveSeconds = 0;
        m_viewDistance = 3;
        join(config, QcWorldGenerators::create(u"showcase"_s, m_content, 0));
        m_plan = QcShowcaseGenerator(m_content).plan();
    }

    void componentsShowTheirPower()
    {
        const QString demo = u"demo-signal_components"_s;
        visit(demo);
        QCOMPARE(block(demo, 2, 1, 1), u"signal_lamp_lit"_s);
        QCOMPARE(block(demo, 5, 1, 1), u"signal_lamp"_s);
        QCOMPARE(block(demo, 17, 1, 1), u"signal_lamp_lit"_s);
        QCOMPARE(property(demo, 21, 1, 1, u"powered"_s), u"true"_s);
        QCOMPARE(block(demo, 23, 1, 1), u"signal_lamp_lit"_s);
        QCOMPARE(property(demo, 26, 1, 1, u"open"_s), u"true"_s);
        QCOMPARE(property(demo, 29, 1, 1, u"open"_s), u"true"_s);
        // The cross next to the lever: 15 at its arm, 14 in the middle; the dot points nowhere.
        QCOMPARE(property(demo, 10, 1, 5, u"power"_s), u"15"_s);
        QCOMPARE(property(demo, 11, 1, 5, u"power"_s), u"14"_s);
        QCOMPARE(property(demo, 11, 1, 5, u"north"_s), u"side"_s);
        QCOMPARE(property(demo, 0, 1, 5, u"north"_s), u"none"_s);
        QCOMPARE(property(demo, 7, 1, 5, u"power"_s), u"0"_s);
        QCOMPARE(property(demo, 14, 1, 5, u"east"_s), u"up"_s);
        // The small Signal Torch: 7 at its dust, 1 at the seventh, which still lights the lamp, and
        // the lamp right on it lights; a lamp on a stone on it stays dark, on the large torch's stone
        // it lights (decision E-745).
        QCOMPARE(property(demo, 2, 1, 8, u"power"_s), u"7"_s);
        QCOMPARE(property(demo, 8, 1, 8, u"power"_s), u"1"_s);
        QCOMPARE(block(demo, 9, 1, 8), u"signal_lamp_lit"_s);
        QCOMPARE(block(demo, 1, 2, 8), u"signal_lamp_lit"_s);
        QCOMPARE(block(demo, 11, 3, 8), u"signal_lamp"_s);
        QCOMPARE(block(demo, 14, 3, 8), u"signal_lamp_lit"_s);
    }

    void quirksHappen()
    {
        const QString demo = u"demo-signal_quirks"_s;
        visit(demo);
        QCOMPARE(property(demo, 8, 1, 4, u"extended"_s), u"false"_s);
        QCOMPARE(property(demo, 12, 1, 4, u"extended"_s), u"true"_s);
        QCOMPARE(block(demo, 22, 1, 5), u"gold_block"_s);
        QCOMPARE(property(demo, 20, 1, 5, u"extended"_s), u"false"_s);
        QCOMPARE(block(demo, 22, 1, 8), u"gold_block"_s);
        QCOMPARE(property(demo, 20, 1, 8, u"extended"_s), u"true"_s);
        // The clock burnt out within its first sixty ticks.
        QCOMPARE(block(demo, 2, 2, 2), u"signal_torch_off"_s);
        QVERIFY(burntOut(origin(demo).offset(2, 2, 2)));
    }

    void pistonsMoveDoorsChestsAndStands()
    {
        const QString demo = u"demo-signal_pistons"_s;
        visit(demo);
        QCOMPARE(block(demo, 3, 1, 3), u"polished_stone"_s);
        QCOMPARE(block(demo, 3, 2, 3), u"polished_stone"_s);
        QCOMPARE(block(demo, 9, 1, 3), u"air"_s);
        QCOMPARE(block(demo, 9, 2, 3), u"air"_s);
        QCOMPARE(block(demo, 17, 1, 3), u"chest"_s);
        const QcBlockEntities::Container *chest = m_server->blockEntities().container(origin(demo).offset(17, 1, 3));
        QVERIFY(chest);
        QCOMPARE(chest->stacks[0].count, 5);
        QVERIFY(!m_server->blockEntities().container(origin(demo).offset(16, 1, 3)));
        const QcBlockPos standCell = origin(demo).offset(17, 1, 3);
        const auto stand = std::ranges::find_if(m_server->stands().stands(), [&](const QcArmourStand &candidate) {
            return std::abs(candidate.motion.position.z - (standCell.z + 0.5)) < 0.01 && candidate.motion.position.y == standCell.y;
        });
        QVERIFY(stand != m_server->stands().stands().end());
        QVERIFY(stand->motion.position.x > standCell.x + 1.0);
        QCOMPARE(block(demo, 38, 1, 8), u"signal_lamp_lit"_s);
    }

    void machinesSortCountObserveAndDispense()
    {
        const QString demo = u"demo-signal_machines"_s;
        visit(demo);
        idle(600);
        const auto countIn = [&](int x, int y, int z, const QString &name) {
            int total = 0;
            if (const QcBlockEntities::Container *box = m_server->blockEntities().container(origin(demo).offset(x, y, z))) {
                for (const QcItemStack &stack : box->stacks) {
                    total += stack.isEmpty() ? 0 : (m_content->item(stack.item).name == u"qtcraft:"_s + name ? stack.count : 0);
                }
            }
            return total;
        };
        // The sorter has sent wheat and cobblestone to their chests, sticks to the overflow.
        QVERIFY(countIn(2, 1, 4, u"wheat"_s) + countIn(2, 2, 4, u"wheat"_s) >= 5);
        QVERIFY(countIn(7, 1, 4, u"cobblestone"_s) + countIn(7, 2, 4, u"cobblestone"_s) >= 5);
        QCOMPARE(countIn(2, 3, 4, u"wheat"_s), 18);
        QCOMPARE(countIn(11, 4, 4, u"wheat"_s), 0);
        // The comparator reads 8 from the half full chest.
        QCOMPARE(property(demo, 15, 1, 4, u"power"_s), u"8"_s);
        QCOMPARE(property(demo, 16, 1, 4, u"power"_s), u"8"_s);
        // The dropper's sticks reach the chest through the hidden collectors.
        QVERIFY(countIn(4, 0, 8, u"stick"_s) > 0);

        // The observer clock keeps going.
        bool on = false;
        bool off = false;
        for (int tick = 0; tick < 12; ++tick) {
            idle(1);
            on = on || property(demo, 16, 1, 8, u"powered"_s) == u"true"_s;
            off = off || property(demo, 16, 1, 8, u"powered"_s) == u"false"_s;
        }
        QVERIFY(on && off);
        // The dispenser's water comes and goes.
        bool water = false;
        bool dry = false;
        for (int tick = 0; tick < 60; ++tick) {
            idle(1);
            water = water || block(demo, 6, 1, 12) == u"water"_s;
            dry = dry || block(demo, 6, 1, 12) == u"air"_s;
        }
        QVERIFY(water && dry);
    }

    void farmRunsHarvestAfterHarvest()
    {
        const QString demo = u"demo-signal_farm"_s;
        visit(demo);
        // The first flush may have begun while the player came near.
        idle(600);
        QCOMPARE(block(demo, 2, 2, 2), u"air"_s);
        // Seeds left on the field planted themselves (decision E-294): young wheat again.
        const int young = planted(demo);
        QVERIFY2(young > 0, qPrintable(QString::number(young)));

        // The showcase does not let plants grow: the field ripens by hand, and the farm runs again,
        // three times; the harvest drifts a little further each time, into the hoppers and the chest.
        for (int cycle = 0; cycle < 3; ++cycle) {
            for (int x = 3; x < 8; ++x) {
                const QcBlockPos cell = origin(demo).offset(x, 2, 2);
                const QcBlockId before = *m_server->world().blockAt(cell);
                const QcBlockId ripe = *m_content->stateId(u"qtcraft:wheat_crop[age=7]"_s);
                QVERIFY(m_server->world().setBlock(cell, ripe));
                m_server->circuitState().noteChange(m_server->world(), cell, before, ripe);
            }
            QVERIFY(flushes(demo));
            const int again = planted(demo);
            QVERIFY2(again > 0, qPrintable(QString::number(again)));
        }
        const int wheat = harvested(demo);
        QVERIFY2(wheat > 0, qPrintable(QString::number(wheat)));
    }

    void railStationRunsByItself()
    {
        const QString demo = u"demo-rails_signal"_s;
        visit(demo);
        QCOMPARE(property(demo, 6, 1, 1, u"powered"_s), u"true"_s);
        QCOMPARE(property(demo, 1, 1, 3, u"powered"_s), u"true"_s);
        QCOMPARE(property(demo, 10, 1, 5, u"powered"_s), u"true"_s);

        // The cart goes round and round: the lamp at the detector rail lights each time.
        int flashes = 0;
        bool lit = false;
        for (int tick = 0; tick < 400; ++tick) {
            idle(1);
            const bool now = block(demo, 2, 1, 6) == u"signal_lamp_lit"_s;
            flashes += now && !lit ? 1 : 0;
            lit = now;
        }
        QVERIFY2(flashes >= 3, qPrintable(QString::number(flashes)));

        // The hoppers under the rails filled their chests from the carts.
        for (const int x : {3, 8}) {
            const QcBlockEntities::Container *chest = m_server->blockEntities().container(origin(demo).offset(x, 0, 11));
            int count = 0;
            for (const QcItemStack &stack : chest ? chest->stacks : QList<QcItemStack>()) {
                count += stack.count;
            }
            QVERIFY2(count > 5, qPrintable(u"chest at %1: %2"_s.arg(x).arg(count)));
        }
    }

    void iceStationJumpsAndSlides()
    {
        const QString demo = u"demo-ice_slide"_s;
        visit(demo);
        const QcBlockPos base = origin(demo);
        const auto localOf = [&](const QcMinecart &cart) { return cart.motion.position - QcVec3{double(base.x), double(base.y), double(base.z)}; };
        const auto cartIn = [&](double fromZ, double toZ) -> const QcMinecart * {
            for (const QcMinecart &cart : m_server->carts().all()) {
                const QcVec3 at = localOf(cart);
                if (at.x >= 0.0 && at.x <= 20.0 && at.z >= fromZ && at.z <= toZ) {
                    return &cart;
                }
            }
            return nullptr;
        };
        QVERIFY(cartIn(0.0, 5.5));

        // The player stands beside the loop, out of the carts' way (walking into one pushes it). The
        // loop cart jumps the gap at x 13 (over it, higher than the track) and lands on the
        // track behind it, round after round.
        int jumps = 0;
        bool over = false;
        for (int tick = 0; tick < 600; ++tick) {
            idle(1);
            const QcMinecart *cart = cartIn(0.0, 5.5);
            QVERIFY(cart);
            const QcVec3 at = localOf(*cart);
            const bool now = at.x > 12.6 && at.x < 13.4 && at.z > 4.0 && at.z < 5.0 && at.y > 1.2;
            jumps += now && !over ? 1 : 0;
            over = now;
        }
        QVERIFY2(jumps >= 2, qPrintable(QString::number(jumps)));

        // The other cart left its track and coasted far along the blue ice, and stands there.
        const QcMinecart *slid = cartIn(8.0, 11.0);
        QVERIFY(slid);
        QVERIFY2(localOf(*slid).x > 8.0, qPrintable(QString::number(localOf(*slid).x)));
        QCOMPARE(slid->motion.velocity.length(), 0.0);
    }
};

QTEST_GUILESS_MAIN(TestSignalShowcase)
#include "test_signal_showcase.moc"
