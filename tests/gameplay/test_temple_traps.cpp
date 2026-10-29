// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "QcServerTestFixture.h"
#include "worldgen/structures/QcStructurePlacement.h"

#include <QTest>

using namespace Qt::StringLiterals;

// The jungle temple's cellar (decisions E-620, E-621) as the generator writes it: its circuit rests
// as generated; the left lever puts out the torch behind the wall, the sticky pistons pull the door
// of the hidden chamber away and push it back when the lever goes off; the two other levers sit on
// dispensers that shoot arrows into the room; the pressure plate in the corridor makes the dispenser
// beside it shoot whoever steps on it. In every turn of the temple. Ticks are stepped by hand.
class TestTempleTraps : public QcServerTestFixture
{
    Q_OBJECT

private:
    static constexpr int arrowsPerDispenser = 8;
    std::optional<QcStructurePlacement> m_temple;

    QcBlockPos cell(int x, int y, int z) const { return m_temple->toWorld(QcBlockPos{x, y, z}); }
    QString nameAt(int x, int y, int z) const { return m_content->block(*m_server->world().blockAt(cell(x, y, z))).name; }
    QString propertyAt(int x, int y, int z, const QString &name) const { return m_content->property(*m_server->world().blockAt(cell(x, y, z)), name); }
    QcServerClient &player() { return *m_server->entities().clients().front(); }
    QcItemStack stack(const char *name, int count) const { return {item(name), count, 0}; }
    qsizetype arrows() const { return m_server->entities().projectiles().all().size(); }

    int arrowsIn(int x, int y, int z) const
    {
        int count = 0;
        for (const QcItemStack &stack : std::as_const(m_server->blockEntities().container(cell(x, y, z))->stacks)) {
            count += stack.isEmpty() ? 0 : stack.count;
        }
        return count;
    }

    // Writes the temple, turned `turns` times, into the air above the stage like the generator does:
    // the blocks as they are, no circuit told; its dispensers hold arrows.
    void buildTemple(int turns)
    {
        join();
        chat(u"/gamerule mobSpawning false"_s);
        chat(u"/gamerule daylightCycle false"_s);
        chat(u"/gamemode survival"_s);
        QcStructure::Loaded structure = QcStructure::fromDefinition(*m_content->worldgen().structure(u"qtcraft:jungle_temple"_s), *m_content);
        QVERIFY(structure);
        m_temple.emplace(std::make_shared<const QcStructure>(std::move(*structure)), m_feet.offset(-8, 24, -8), turns, *m_content);

        for (int x = -1; x <= m_temple->sizeX() + 1; x += 4) {
            for (int z = -1; z <= m_temple->sizeZ() + 1; z += 4) {
                m_server->world().loadChunk(m_temple->origin().offset(x, 0, z).chunk());
            }
        }
        const QcStructure &shape = m_temple->structure();
        for (int y = 0; y < shape.sizeY(); ++y) {
            for (int z = 0; z < shape.sizeZ(); ++z) {
                for (int x = 0; x < shape.sizeX(); ++x) {
                    if (const std::optional<QcBlockId> block = m_temple->worldBlock({x, y, z})) {
                        QVERIFY(m_server->world().setBlock(m_temple->toWorld(QcBlockPos{x, y, z}), *block));
                    }
                }
            }
        }
        m_server->circuitState().clear();
        for (const auto &[x, z] : {std::pair{9, 3}, {11, 3}, {12, 11}}) {
            QVERIFY(ensureEntity(cell(x, 1, z)));
            m_server->blockEntities().container(cell(x, 1, z))->stacks[0] = stack("qtcraft:arrow", arrowsPerDispenser);
        }
    }

    void flip(int x, int y, int z) { QVERIFY(m_server->systems().circuits.use(cell(x, y, z))); }

    void verifyDoorShut()
    {
        QCOMPARE(nameAt(5, 1, 5), u"qtcraft:mossy_cobblestone"_s);
        QCOMPARE(nameAt(5, 2, 5), u"qtcraft:mossy_cobblestone"_s);
        QCOMPARE(nameAt(6, 1, 3), u"qtcraft:signal_torch"_s);
        QCOMPARE(propertyAt(5, 1, 3, u"extended"_s), u"true"_s);
        QCOMPARE(propertyAt(5, 2, 3, u"extended"_s), u"true"_s);
    }

    // The speed of `velocity` along the world direction of the structure's step from (x, z) to
    // (toX, toZ) on the same layer.
    double speedAlong(const QcVec3 &velocity, std::pair<int, int> from, std::pair<int, int> to) const
    {
        const QcBlockPos start = cell(from.first, 1, from.second);
        const QcBlockPos end = cell(to.first, 1, to.second);
        return velocity.x * (end.x - start.x) + velocity.z * (end.z - start.z);
    }

    static void addTurns()
    {
        QTest::addColumn<int>("turns");
        for (int turns = 0; turns < 4; ++turns) {
            QTest::addRow("%d turns", turns) << turns;
        }
    }

private slots:
    void theCellarRestsAsGenerated_data() { addTurns(); }
    void theCellarRestsAsGenerated()
    {
        QFETCH(int, turns);
        buildTemple(turns);
        ticks(40);
        verifyDoorShut();
        QCOMPARE(arrows(), 0);
        QCOMPARE(propertyAt(7, 1, 4, u"powered"_s), u"false"_s);
    }

    void theLeftLeverOpensTheChamber_data() { addTurns(); }
    void theLeftLeverOpensTheChamber()
    {
        QFETCH(int, turns);
        buildTemple(turns);
        flip(7, 1, 4);
        ticks(12);
        // The torch went out, the pistons pulled the door into their heads' place: the way is open.
        QCOMPARE(nameAt(6, 1, 3), u"qtcraft:signal_torch_off"_s);
        QCOMPARE(m_content->block(*m_server->world().blockAt(cell(5, 1, 5))).name, u"qtcraft:air"_s);
        QCOMPARE(nameAt(5, 2, 5), u"qtcraft:air"_s);
        QCOMPARE(nameAt(5, 1, 4), u"qtcraft:mossy_cobblestone"_s);
        QCOMPARE(nameAt(5, 2, 4), u"qtcraft:mossy_cobblestone"_s);
        QCOMPARE(propertyAt(5, 1, 3, u"extended"_s), u"false"_s);
        QCOMPARE(arrows(), 0);

        // Back off: the torch lights again and the pistons push the door shut.
        flip(7, 1, 4);
        ticks(12);
        verifyDoorShut();
        QCOMPARE(nameAt(5, 1, 4), u"qtcraft:signal_piston_head"_s);
    }

    void theOtherLeversShootArrows_data() { addTurns(); }
    void theOtherLeversShootArrows()
    {
        QFETCH(int, turns);
        buildTemple(turns);
        flip(9, 1, 4);
        ticks(8);
        QCOMPARE(arrows(), 1);
        const QcProjectile &arrow = m_server->entities().projectiles().all().front();
        QCOMPARE(arrow.type, u"qtcraft:arrow"_s);
        // Into the room, away from the wall it sits in (the structure's south, turned with it).
        const double along = speedAlong(arrow.motion.velocity, {9, 3}, {9, 4});
        QVERIFY2(along > 15.0, qPrintable(QString::number(along)));
        QCOMPARE(arrowsIn(9, 1, 3), arrowsPerDispenser - 1);

        flip(11, 1, 4);
        ticks(8);
        QCOMPARE(arrows(), 2);
        QCOMPARE(arrowsIn(11, 1, 3), arrowsPerDispenser - 1);
        // The door stays shut.
        verifyDoorShut();
    }

    void thePlateShootsWhoStepsOnIt_data() { addTurns(); }
    void thePlateShootsWhoStepsOnIt()
    {
        QFETCH(int, turns);
        buildTemple(turns);
        const QcBlockPos plate = cell(11, 1, 11);
        chat(u"/tp %1 %2 %3"_s.arg(plate.x + 0.5).arg(plate.y).arg(plate.z + 0.5));
        const double health = player().health;
        ticks(12);
        QCOMPARE(propertyAt(11, 1, 11, u"powered"_s), u"true"_s);
        QCOMPARE(arrowsIn(12, 1, 11), arrowsPerDispenser - 1);
        QVERIFY2(player().health < health, qPrintable(QString::number(player().health)));
    }

    void anArrowFromATrapCanBePickedUp()
    {
        // Dispenser arrows are ammunition like a player's: they stick and come back.
        buildTemple(0);
        flip(9, 1, 4);
        ticks(30);
        QCOMPARE(arrows(), 1);
        QCOMPARE(m_server->entities().projectiles().all().front().pickup, QcProjectilePickup::Item);
    }
};

QTEST_GUILESS_MAIN(TestTempleTraps)
#include "test_temple_traps.moc"
