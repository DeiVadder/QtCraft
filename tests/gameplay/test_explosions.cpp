// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fixtures/QcMachineFixture.h"
#include "fixtures/QcTestUtil.h"
#include "server/entities/QcEntityChecks.h"
#include "server/explosions/QcBlastField.h"
#include "server/explosions/QcExplosionRays.h"
#include "server/explosions/QcServerExplosions.h"
#include "server/storage/QcEntityCodec.h"
#include "worldgen/structures/QcStructure.h"

#include <QElapsedTimer>

using namespace Qt::StringLiterals;

// Explosives and explosions (stage 51.2, decisions E-600 to E-604): what lights an explosive (flint
// and steel, a dispenser, signal power, a pressure plate, fire, another explosion), the crater of a
// blast by blast resistance and its dice, what it does to bodies behind walls and in the open, the
// game rule explosionBlockDamage, the limits (lit explosives, explosions per tick, blocks per
// explosion and per tick) in a field of explosives, saving a lit explosive and the batch the
// clients get. Ticks are stepped by hand.
class TestExplosions : public QcMachineFixture
{
    Q_OBJECT

private:
    QcServerExplosions &explosions() { return m_server->systems().explosions; }
    QcBlockId block(QcBlockPos pos) const { return m_server->world().blockAt(pos).value_or(qcAirBlockId); }
    bool isAir(QcBlockPos pos) const { return block(pos) == qcAirBlockId; }
    qsizetype primed() const { return m_server->entities().explosives().all().size(); }

    // Ticks until no explosive is lit and no cell of a blast waits, at most `most` ticks; returns
    // the ticks it took.
    int settle(int most = 400)
    {
        for (int tick = 0; tick < most; ++tick) {
            if (primed() == 0 && explosions().waitingCells() == 0) {
                return tick;
            }
            ticks(1);
        }
        return most;
    }

    // A solid cube of `name` with edge `size` whose lowest north west corner is `corner`.
    void fillCube(QcBlockPos corner, int size, const char *name)
    {
        for (int x = 0; x < size; ++x) {
            for (int y = 0; y < size; ++y) {
                for (int z = 0; z < size; ++z) {
                    QVERIFY(m_server->world().setBlock(corner.offset(x, y, z), id(name)));
                }
            }
        }
    }

    // How many cells of the cube changed from `name`.
    int changedIn(QcBlockPos corner, int size, const char *name) const
    {
        int changed = 0;
        for (int x = 0; x < size; ++x) {
            for (int y = 0; y < size; ++y) {
                for (int z = 0; z < size; ++z) {
                    changed += block(corner.offset(x, y, z)) != id(name) ? 1 : 0;
                }
            }
        }
        return changed;
    }

    // A blast of power 4 in the middle of a 9 × 9 × 9 cube of `name` whose middle cell is air.
    QList<QcBlockPos> crater(const char *name, quint64 seed)
    {
        const QcBlockPos corner = at(2, 0, 2).offset(0, 20, 0);
        fillCube(corner, 9, name);
        const QcBlockPos middle = corner.offset(4, 4, 4);
        m_server->world().setBlock(middle, qcAirBlockId);
        QcBlastField field(m_server->world(), explosions().table(), middle, 10);
        return QcExplosionRays::broken(field, {{middle.x + 0.5, middle.y + 0.5, middle.z + 0.5}, 4.0, seed}, 4096);
    }

    void holdFlintAndSteel()
    {
        chat(u"/give flint_and_steel 1"_s);
        ticks(1);
        const int slot = slotOf(item("qtcraft:flint_and_steel"));
        QVERIFY(slot >= 0 && slot < QcInventory::hotbarSize);
        send(QcMsg::SelectHotbarSlot{slot});
    }

    // Whether `pos` holds what it held before the station's blast: the station's block (but for the
    // chimney's filling and the lever, which go), else the area's stone floor or air.
    bool keptBesideChimney(const QcStructure &station, QcBlockPos origin, QcBlockPos pos) const
    {
        const int x = pos.x - origin.x;
        const int y = pos.y - origin.y;
        const int z = pos.z - origin.z;
        const bool inStation = station.contains(x, y, z);
        if (inStation && x == 2 && ((z == 2 && y >= 1) || (z == 4 && y == 1))) {
            return true;
        }
        const std::optional<QcBlockId> planned = inStation ? station.block(x, y, z) : std::nullopt;
        if (planned) {
            return block(pos) == *planned;
        }
        return block(pos) == (pos.y == m_origin.y - 1 ? id("qtcraft:stone") : qcAirBlockId);
    }

    // Moves the raw player to the middle of the cell `pos`, standing on its floor.
    void stand(QcBlockPos pos)
    {
        chat(u"/tp %1 %2 %3"_s.arg(pos.x + 0.5).arg(pos.y).arg(pos.z + 0.5));
        idle(2);
    }

private slots:
    void rayCraterFollowsBlastResistance()
    {
        buildArea();
        // Dirt (0.5) breaks far, stone (6) only near the centre, obsidian (1200) never.
        const qsizetype dirt = crater("qtcraft:dirt", 7).size();
        const qsizetype stone = crater("qtcraft:stone", 7).size();
        const qsizetype obsidian = crater("qtcraft:obsidian", 7).size();
        QVERIFY2(dirt > stone, qPrintable(u"dirt %1 stone %2"_s.arg(dirt).arg(stone)));
        QVERIFY(stone > 0);
        QCOMPARE(obsidian, qsizetype(0));
        // Crying obsidian (1200) resists like obsidian.
        QCOMPARE(crater("qtcraft:crying_obsidian", 7).size(), qsizetype(0));
        // The same blast in the same world breaks the same cells, another seed others.
        QCOMPARE(crater("qtcraft:dirt", 7), crater("qtcraft:dirt", 7));
        QVERIFY(crater("qtcraft:dirt", 7) != crater("qtcraft:dirt", 8));
        // Nearest first.
        const QList<QcBlockPos> cells = crater("qtcraft:stone", 7);
        const QcBlockPos middle = at(2, 0, 2).offset(4, 24, 4);
        const auto distance = [&middle](QcBlockPos pos) {
            return std::abs(pos.x - middle.x) + std::abs(pos.y - middle.y) + std::abs(pos.z - middle.z);
        };
        QVERIFY(distance(cells.first()) <= distance(cells.last()));
        // A blast in water breaks nothing: the water soaks up every ray.
        const QcBlockPos corner = at(2, 0, 2).offset(0, 20, 0);
        fillCube(corner, 9, "qtcraft:dirt");
        const QcBlockPos centre = corner.offset(4, 4, 4);
        m_server->world().setBlock(centre, id("qtcraft:water"));
        QcBlastField field(m_server->world(), explosions().table(), centre, 10);
        QVERIFY(QcExplosionRays::broken(field, {{centre.x + 0.5, centre.y + 0.5, centre.z + 0.5}, 4.0, 7}, 4096).isEmpty());
    }

    void flintAndSteelLightsAndTheFuseRunsOut()
    {
        buildArea();
        const QcBlockPos explosive = at(7, 0, 7);
        put(7, 0, 7, u"explosive"_s);
        stand(at(7, 0, 3));
        holdFlintAndSteel();
        send(QcMsg::UseItem{explosive, QcBlockFace::Up});
        QVERIFY(isAir(explosive));
        QCOMPARE(primed(), qsizetype(1));
        QCOMPARE(m_server->entities().explosives().all().first().fuse, 79);
        QCOMPARE(m_client->stacks[slotOf(item("qtcraft:flint_and_steel"))].damage, 1);

        // Away before it goes off: 80 ticks of fuse.
        stand(at(0, 0, 0).offset(-40, 0, 0));
        const int left = m_server->entities().explosives().all().first().fuse;
        ticks(left - 1);
        QCOMPARE(explosions().numbers().explosions, quint64(0));
        ticks(1);
        QCOMPARE(explosions().numbers().explosions, quint64(1));
        QCOMPARE(primed(), qsizetype(0));
        settle();
        // The stone floor under it has a hole.
        QVERIFY(isAir(at(7, -1, 7)));
    }

    void powerAndPlatesLightExplosives()
    {
        buildArea();
        put(3, 0, 3, u"explosive"_s);
        put(4, 0, 3, u"lever[attach=floor,powered=true]"_s);
        ticks(2);
        QVERIFY(isAir(at(3, 0, 3)));
        QCOMPARE(explosions().numbers().lit, quint64(1));

        // A plate standing on an explosive lights it when someone steps on it (the coastal trap).
        put(10, 0, 10, u"explosive"_s);
        put(10, 1, 10, u"stone_pressure_plate"_s);
        ticks(2);
        QCOMPARE(explosions().numbers().lit, quint64(1));
        stand(at(10, 1, 10));
        ticks(2);
        QCOMPARE(explosions().numbers().lit, quint64(2));
        QVERIFY(isAir(at(10, 0, 10)));
    }

    void dispenserAndFireLightExplosives()
    {
        buildArea();
        // A dispenser with flint and steel facing an explosive lights it and keeps the tool.
        put(2, 0, 2, u"signal_dispenser[facing=east]"_s);
        put(3, 0, 2, u"explosive"_s);
        fill(2, 0, 2, {stack("qtcraft:flint_and_steel", 1)});
        put(2, 0, 1, u"lever[attach=floor,powered=true]"_s);
        ticks(8);
        QVERIFY(isAir(at(3, 0, 2)));
        QCOMPARE(explosions().numbers().lit, quint64(1));
        QCOMPARE(countIn(2, 0, 2, "qtcraft:flint_and_steel"), 1);

        // Fire next to an explosive lights it instead of burning it away.
        put(10, 0, 10, u"explosive"_s);
        QVERIFY(m_server->systems().fire.ignite(at(10, 0, 11)));
        for (int tick = 0; tick < 2000 && explosions().numbers().lit < 2; ++tick) {
            ticks(1);
            if (isAir(at(10, 0, 11)) && block(at(10, 0, 10)) == id("qtcraft:explosive")) {
                m_server->systems().fire.ignite(at(10, 0, 11));
            }
        }
        QCOMPARE(explosions().numbers().lit, quint64(2));
    }

    void chainsSpreadOverTicks()
    {
        buildArea();
        for (int x = 2; x < 12; ++x) {
            put(x, 0, 7, u"explosive"_s);
        }
        QVERIFY(explosions().light(at(2, 0, 7), 1));
        const int took = settle();
        QVERIFY(took < 400);
        QCOMPARE(explosions().numbers().explosions, quint64(10));
        QCOMPARE(explosions().numbers().chained, quint64(9));
        for (int x = 2; x < 12; ++x) {
            QVERIFY(block(at(x, 0, 7)) != id("qtcraft:explosive"));
        }
        // The clients got the blocks as batches.
        QVERIFY(m_client->blockBatches > 0);
        QCOMPARE(m_client->explosions.size(), qsizetype(10));
    }

    void limitsHoldInAFieldOfExplosives()
    {
        QcServerConfig config;
        config.limits.primedExplosives = 64;
        config.limits.explosionsPerTick = 2;
        config.limits.explosionBlocks = 40;
        config.limits.explosionBlocksPerTick = 96;
        buildArea(config);
        // A field of 12 × 12 × 2 explosives on the stone floor.
        for (int x = 1; x < 13; ++x) {
            for (int z = 1; z < 13; ++z) {
                put(x, 0, z, u"explosive"_s);
                put(x, 1, z, u"explosive"_s);
            }
        }
        QVERIFY(explosions().light(at(6, 0, 6), 1));

        quint64 explosionsBefore = 0;
        quint64 brokenBefore = 0;
        int tick = 0;
        for (; tick < 3000 && (primed() > 0 || explosions().waitingCells() > 0); ++tick) {
            ticks(1);
            const QcServerExplosions::Numbers &numbers = explosions().numbers();
            QVERIFY(numbers.explosions - explosionsBefore <= 2);
            QVERIFY(explosions().blocksBroken() - brokenBefore <= 96);
            QVERIFY(primed() <= 64);
            explosionsBefore = numbers.explosions;
            brokenBefore = explosions().blocksBroken();
        }
        QVERIFY2(tick < 3000, "the chain did not end");
        // Every explosive explodes once: the field and nothing more.
        QCOMPARE(explosions().numbers().explosions, explosions().numbers().lit);
        // Nothing is lost on the way: every explosive of the field was lit or is still a block (one
        // refused at the limit stays where it is).
        quint64 left = 0;
        for (int x = -1; x <= areaSize; ++x) {
            for (int y = -1; y < areaHeight; ++y) {
                for (int z = -1; z <= areaSize; ++z) {
                    left += block(m_origin.offset(x, y, z)) == id("qtcraft:explosive") ? 1 : 0;
                }
            }
        }
        QCOMPARE(left + explosions().numbers().lit, quint64(12 * 12 * 2));
        QVERIFY(explosions().numbers().explosions > 20);
        QVERIFY(explosions().numbers().waited > 0);
        QVERIFY(QcEntityChecks::problems(m_server->entities()).isEmpty());
    }

    void gameRuleKeepsBlocks()
    {
        buildArea();
        chat(u"/gamerule explosionBlockDamage false"_s);
        put(5, 0, 5, u"stone"_s);
        put(7, 0, 5, u"explosive"_s);
        QVERIFY(explosions().light(at(6, 0, 5), 1) == false);
        put(6, 0, 5, u"explosive"_s);
        QVERIFY(explosions().light(at(6, 0, 5), 1));
        settle();
        QCOMPARE(explosions().numbers().explosions, quint64(1));
        QCOMPARE(block(at(5, 0, 5)), id("qtcraft:stone"));
        QCOMPARE(block(at(7, 0, 5)), id("qtcraft:explosive"));
        QVERIFY(block(at(6, -1, 5)) == id("qtcraft:stone"));
    }

    void bodiesBehindWallsAreSafe()
    {
        buildArea();
        chat(u"/gamemode survival"_s);
        idle(2);
        // In the open, three blocks away: hurt and pushed.
        const double full = m_client->health;
        stand(at(6, 0, 4));
        put(6, 0, 7, u"obsidian"_s);
        explosions().explode({QcVec3{at(6, 0, 7).x + 0.5, static_cast<double>(at(6, 1, 7).y) + 0.5, at(6, 0, 7).z + 0.5}, 4.0, false, false});
        idle(2);
        QVERIFY2(m_client->health < full, qPrintable(u"health %1"_s.arg(m_client->health)));
        QCOMPARE(explosions().numbers().playersHit, quint64(1));

        // Behind a wall of obsidian: nothing.
        chat(u"/effect clear"_s);
        idle(30);
        for (int x = 3; x <= 9; ++x) {
            for (int y = 0; y < 4; ++y) {
                put(x, y, 5, u"obsidian"_s);
            }
        }
        stand(at(6, 0, 3));
        explosions().explode({QcVec3{at(6, 0, 8).x + 0.5, static_cast<double>(at(6, 0, 8).y) + 0.5, at(6, 0, 8).z + 0.5}, 4.0, false, false});
        idle(2);
        QCOMPARE(explosions().numbers().playersHit, quint64(1));
    }

    void showcaseStationKeepsTheBlastInItsChimney()
    {
        // The showcase station (decision E-604): the lever lights the explosive at the foot of an
        // obsidian chimney; the blast clears the dirt, sand and gravel in the chimney and nothing
        // else, and the player at the lever takes no damage.
        buildArea();
        chat(u"/gamemode survival"_s);
        const auto definition = std::ranges::find(m_content->worldgen().structures(), u"qtcraft:explosives_station"_s, &QcStructureDefinition::name);
        QVERIFY(definition != m_content->worldgen().structures().end());
        const QcStructure::Loaded station = QcStructure::fromDefinition(*definition, *m_content);
        QVERIFY(station.has_value());
        // Local y 0 is the ground layer: the area's stone floor.
        const QcBlockPos origin = at(4, -1, 2);
        for (int x = 0; x < station->sizeX(); ++x) {
            for (int y = 0; y < station->sizeY(); ++y) {
                for (int z = 0; z < station->sizeZ(); ++z) {
                    if (const std::optional<QcBlockId> state = station->block(x, y, z)) {
                        putAt(origin.offset(x, y, z), *state);
                    }
                }
            }
        }
        ticks(2);
        QCOMPARE(explosions().numbers().lit, quint64(0));
        const QcBlockPos lever = origin.offset(2, 1, 4);
        QVERIFY(m_content->block(block(lever)).signal.kind == QcSignalKind::Lever);

        stand(origin.offset(2, 1, 6));
        const double health = m_client->health;
        send(QcMsg::UseItem{lever, QcBlockFace::South});
        ticks(2);
        QCOMPARE(explosions().numbers().lit, quint64(1));
        settle();
        idle(2);
        QCOMPARE(explosions().numbers().explosions, quint64(1));
        QCOMPARE(m_client->health, health);
        for (int y = 2; y <= 4; ++y) {
            QVERIFY(isAir(origin.offset(2, y, 2)));
        }
        // Every obsidian block and every cell of the area outside the station stays.
        for (int x = -1; x < areaSize + 1; ++x) {
            for (int z = -1; z < areaSize + 1; ++z) {
                for (int y = -1; y < areaHeight; ++y) {
                    QVERIFY2(keptBesideChimney(*station, origin, m_origin.offset(x, y, z)), qPrintable(u"%1 %2 %3"_s.arg(x).arg(y).arg(z)));
                }
            }
        }
    }

    void coastalTowerTrapFires()
    {
        // The trap of the coastal tower (decision E-605): stepping on the plate in front of the
        // chest lights the explosive under it, and the chain takes the other three with it.
        buildArea();
        chat(u"/gamemode survival"_s);
        const auto definition = std::ranges::find(m_content->worldgen().structures(), u"qtcraft:coastal_tower"_s, &QcStructureDefinition::name);
        QVERIFY(definition != m_content->worldgen().structures().end());
        const QcStructure::Loaded tower = QcStructure::fromDefinition(*definition, *m_content);
        QVERIFY(tower.has_value());
        const QcBlockPos origin = at(3, 20, 3);
        for (int x = 0; x < tower->sizeX(); ++x) {
            for (int y = 0; y < tower->sizeY(); ++y) {
                for (int z = 0; z < tower->sizeZ(); ++z) {
                    if (const std::optional<QcBlockId> state = tower->block(x, y, z)) {
                        putAt(origin.offset(x, y, z), *state);
                    }
                }
            }
        }
        ticks(4);
        QCOMPARE(explosions().numbers().lit, quint64(0));
        QCOMPARE(block(origin.offset(3, 12, 2)), id("qtcraft:explosive"));

        stand(origin.offset(3, 13, 2));
        QVERIFY(explosions().numbers().lit >= 1);
        stand(at(0, 0, 0).offset(-40, 0, 0));
        settle();
        QCOMPARE(explosions().numbers().explosions, quint64(4));
        QCOMPARE(explosions().numbers().lit, quint64(4));
    }

    void measureChain()
    {
        // QTCRAFT_EXPLOSION_MEASURE=<n>: a field of n × n × 2 explosives with the default limits, lit
        // at one corner and ticked by hand until it is over; prints the ticks, the explosions and the
        // tick times (mean, worst, the phase explosions). Not part of the normal run.
        const int size = qEnvironmentVariableIntValue("QTCRAFT_EXPLOSION_MEASURE");
        if (size <= 0) {
            QSKIP("set QTCRAFT_EXPLOSION_MEASURE=<n> for a field of n x n x 2");
        }
        join();
        chat(u"/gamerule mobSpawning false"_s);
        const QcBlockPos corner = m_feet.offset(-size / 2, 20, 10);
        loadAround(corner.offset(size / 2, 0, size / 2), size / QcWorld::chunkSize + 2);
        for (int x = 0; x < size; ++x) {
            for (int z = 0; z < size; ++z) {
                m_server->world().setBlock(corner.offset(x, -1, z), id("qtcraft:stone"));
                m_server->world().setBlock(corner.offset(x, 0, z), id("qtcraft:explosive"));
                m_server->world().setBlock(corner.offset(x, 1, z), id("qtcraft:explosive"));
            }
        }
        QVERIFY(explosions().light(corner, 1));

        QElapsedTimer timer;
        qint64 totalNs = 0;
        qint64 worstNs = 0;
        int count = 0;
        for (; count < 20000 && (primed() > 0 || explosions().waitingCells() > 0); ++count) {
            timer.start();
            m_server->tick();
            const qint64 ns = timer.nsecsElapsed();
            QcTestUtil::pump();
            totalNs += ns;
            worstNs = std::max(worstNs, ns);
        }
        qInfo().noquote() << u"EXPLOSIONS field %1x%1x2 ticks %2 explosions %3 chained %4 refused %5 waited %6 broken %7 tick mean %8 ms worst %9 ms"_s
                                 .arg(size)
                                 .arg(count)
                                 .arg(explosions().numbers().explosions)
                                 .arg(explosions().numbers().chained)
                                 .arg(explosions().numbers().refused)
                                 .arg(explosions().numbers().waited)
                                 .arg(explosions().blocksBroken())
                                 .arg(static_cast<double>(totalNs) / std::max(1, count) / 1e6, 0, 'f', 2)
                                 .arg(static_cast<double>(worstNs) / 1e6, 0, 'f', 2);
    }

    void exposureAndImpact()
    {
        // The formulas of the reference game at their ends.
        QCOMPARE(QcBlastBodies::impact(8.0, 4.0, 1.0), 0.0);
        QCOMPARE(QcBlastBodies::impact(0.0, 4.0, 0.5), 0.5);
        QCOMPARE(QcBlastBodies::damageOf(1.0, 4.0), 57.0);
        QCOMPARE(QcBlastBodies::damageOf(0.0, 4.0), 1.0);
    }

    void litExplosivesAreSavedAsFallingBlocksWithAFuse()
    {
        QcEntityRecord record;
        record.chunk = {0, 0};
        QcPrimedExplosive primed;
        primed.block = *m_content->blockId(u"qtcraft:explosive"_s);
        primed.fuse = 33;
        primed.motion.position = {3.5, 70.0, 4.5};
        record.primedExplosives.append({primed, 1});
        QcFallingBlock falling;
        falling.block = *m_content->blockId(u"qtcraft:sand"_s);
        falling.motion.position = {5.5, 70.0, 5.5};
        record.fallingBlocks.append({falling, 2});

        QcLoadWarnings warnings;
        const QcResult<QcEntityRecord> read = QcEntityCodec::decode(QcEntityCodec::encode(record, *m_content), {0, 0}, *m_content, warnings);
        QVERIFY(read.has_value());
        QCOMPARE(read->primedExplosives.size(), qsizetype(1));
        QCOMPARE(read->primedExplosives.first().entry.fuse, 33);
        QCOMPARE(read->primedExplosives.first().entry.block, primed.block);
        QCOMPARE(read->fallingBlocks.size(), qsizetype(1));
        QVERIFY(warnings.texts().isEmpty());
    }
};

QTEST_MAIN(TestExplosions)
#include "test_explosions.moc"
