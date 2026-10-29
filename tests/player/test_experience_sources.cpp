// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/gameplay/QcCrafting.h"
#include "common/gameplay/QcMining.h"
#include "fixtures/QcServerTestFixture.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QMap>

using namespace Qt::StringLiterals;

// Where experience comes from (stage 36, decision E-398): ores mined with a tool that gets their
// drops, creatures a player kills, a furnace's smelting when a player takes the output or the
// furnace breaks, bred animals; and lapis lazuli: the ore deep in the generated world, its 4 to 9
// drops, the block and dye recipes. Experience is counted wherever it is, in orbs and in the
// player, so it does not matter whether the player collected the orbs yet. Ticks are stepped by hand.
class TestExperienceSources : public QcServerTestFixture
{
    Q_OBJECT

private:
    QcServerClient &player() { return *m_server->entities().clients().front(); }

    // Points in orbs and gathered by the player.
    qint64 gathered() { return m_server->orbs().totalPoints() + player().experience.total(); }

    void forget()
    {
        m_server->orbs().clear();
        player().experience = {};
    }

    void hold(const QString &name)
    {
        chat(u"/give "_s + name);
        const int slot = slotOf(item(qPrintable(u"qtcraft:"_s + name)));
        QVERIFY(slot >= 0 && slot < QcInventory::hotbarSize);
        send(QcMsg::SelectHotbarSlot{slot});
    }

    // Mines the block at `pos` with the held tool, waiting as long as mining it takes.
    void mine(QcBlockPos pos)
    {
        const QcBlockDefinition &block = m_content->block(*m_server->world().blockAt(pos));
        const QcItemStack &held = m_client->stacks[m_client->selected];
        const QcItemDefinition *tool = held.isEmpty() ? nullptr : &m_content->item(held.item);
        send(QcMsg::StartMining{pos});
        ticks(static_cast<int>(std::ceil(QcMining::breakSeconds(block, tool) * QcPhysics::tickRate)) + 1);
        send(QcMsg::FinishMining{pos});
        QCOMPARE(*m_server->world().blockAt(pos), qcAirBlockId);
    }

    // Points from mining `ore` `times` times next to the player, lowest and highest (seen, not the
    // bounds: the drops are random).
    std::pair<qint64, qint64> mined(const char *ore, int times)
    {
        qint64 low = std::numeric_limits<qint64>::max();
        qint64 high = 0;
        const QcBlockPos pos = m_feet.offset(1, 0, 0);
        for (int i = 0; i < times; ++i) {
            forget();
            m_server->world().setBlock(pos, id(ore));
            mine(pos);
            ticks(1);
            low = std::min(low, gathered());
            high = std::max(high, gathered());
        }
        return {low, high};
    }

    // Lets the furnace east of the player smelt `count` raw iron with enough coal.
    QcBlockPos smeltIron(int count)
    {
        const QcBlockPos pos = openFurnace();
        chat(u"/give raw_iron %1"_s.arg(count));
        chat(u"/give coal 2"_s);
        move(slotOf(item("qtcraft:raw_iron")), inputSlot);
        move(slotOf(item("qtcraft:coal")), fuelSlot);
        for (int i = 0; i < 20 * 12 * count && m_server->blockEntities().machine(pos)->stacks[2].count < count; ++i) {
            ticks(1);
        }
        return pos;
    }

    // The lapis lazuli ore of a generated chunk, in local coordinates, below height 64.
    QList<QcBlockPos> lapisIn(const QcTerrainGenerator &generator, QcChunkPos pos) const
    {
        const QcBlockId lapis = id("qtcraft:lapis_ore");
        const auto chunk = generator.generate(pos);
        QList<QcBlockPos> found;
        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                for (int y = 0; y < 64; ++y) {
                    if (chunk->block(x, y, z) == lapis) {
                        found.append({x, y, z});
                    }
                }
            }
        }
        return found;
    }

    QString crafted(const QList<const char *> &cells, int size) const
    {
        QList<QcItemStack> grid(static_cast<qsizetype>(size) * size);
        for (qsizetype i = 0; i < cells.size(); ++i) {
            grid[i] = cells[i] ? QcItemStack{item(cells[i]), 1, 0} : QcItemStack();
        }
        const std::optional<qsizetype> recipe = QcCrafting::match(*m_content, grid, size);
        return recipe ? u"%1 %2"_s.arg(m_content->recipes()[*recipe].resultCount).arg(m_content->recipes()[*recipe].resultItem) : QString();
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void oresGiveExperienceWithTheRightTool()
    {
        join();
        hold(u"diamond_pickaxe"_s);
        const auto within = [](std::pair<qint64, qint64> seen, qint64 low, qint64 high) {
            return seen.first >= low && seen.second <= high && seen.first < seen.second;
        };
        QVERIFY(within(mined("qtcraft:coal_ore", 12), 0, 2));
        QVERIFY(within(mined("qtcraft:diamond_ore", 12), 3, 7));
        QVERIFY(within(mined("qtcraft:lapis_ore", 12), 2, 5));
        QVERIFY(within(mined("qtcraft:signal_ore", 12), 1, 5));
        QCOMPARE(mined("qtcraft:stone", 2), (std::pair<qint64, qint64>{0, 0}));

        // Without a tool that gets the drops there is no experience either.
        hold(u"wooden_pickaxe"_s);
        QCOMPARE(mined("qtcraft:diamond_ore", 2), (std::pair<qint64, qint64>{0, 0}));
    }

    void lapisDropsFourToNine()
    {
        join();
        hold(u"iron_pickaxe"_s);
        const QcItemId lapis = item("qtcraft:lapis_lazuli");
        QMap<int, int> counts;
        for (int i = 0; i < 40; ++i) {
            const int before = countOf(lapis) + droppedCount(lapis);
            m_server->world().setBlock(m_feet.offset(1, 0, 0), id("qtcraft:lapis_ore"));
            mine(m_feet.offset(1, 0, 0));
            ++counts[countOf(lapis) + droppedCount(lapis) - before];
        }
        QCOMPARE(counts.firstKey(), 4);
        QCOMPARE(counts.lastKey(), 9);
    }

    void creaturesKilledByAPlayerGiveTheirExperience()
    {
        join();
        const std::optional<int> zombie = m_content->mobIndex(u"qtcraft:zombie"_s);
        QVERIFY(zombie);
        const quint32 id = m_server->mobs().spawn(*zombie, {m_feet.x + 2.5, static_cast<double>(m_feet.y), m_feet.z + 0.5}, m_server->entities().newId());
        m_server->mobs().find(id)->health = 0.5;
        send(QcMsg::AttackEntity{id});
        QVERIFY(!m_server->mobs().find(id));
        QCOMPARE(gathered(), 5);

        // A pig gives 1 to 3 (after the attack cooldown).
        forget();
        ticks(20);
        const std::optional<int> pig = m_content->mobIndex(u"qtcraft:pig"_s);
        const quint32 pigId = m_server->mobs().spawn(*pig, {m_feet.x + 2.5, static_cast<double>(m_feet.y), m_feet.z + 0.5}, m_server->entities().newId());
        m_server->mobs().find(pigId)->health = 0.5;
        send(QcMsg::AttackEntity{pigId});
        QVERIFY(gathered() >= 1 && gathered() <= 3);

        // A piglet gives none.
        forget();
        ticks(20);
        const quint32 pigletId = m_server->mobs().spawn(*pig, {m_feet.x + 2.5, static_cast<double>(m_feet.y), m_feet.z + 0.5}, m_server->entities().newId());
        m_server->mobs().find(pigletId)->growUp = 600.0;
        m_server->mobs().find(pigletId)->health = 0.5;
        send(QcMsg::AttackEntity{pigletId});
        QVERIFY(!m_server->mobs().find(pigletId));
        ticks(1);
        QCOMPARE(gathered(), 0);
    }

    void smeltingGathersInTheFurnaceUntilTheOutputIsTaken()
    {
        join();
        const QcBlockPos pos = smeltIron(3);
        const QcBlockEntities::Machine *machine = m_server->blockEntities().machine(pos);
        QCOMPARE(machine->stacks[2].count, 3);
        QVERIFY(std::abs(machine->experience - 2.1) < 1e-9);
        QCOMPARE(gathered(), 0);

        // Taking one ingot releases all of it: two points, a third with a chance of a tenth.
        move(outputSlot, 20, 1);
        QCOMPARE(machine->experience, 0.0);
        QVERIFY(gathered() >= 2 && gathered() <= 3);
    }

    // Shift-clicking the output into the inventory releases the experience like dragging it.
    void shiftClickingTheOutputReleasesIt()
    {
        join();
        const QcBlockPos pos = smeltIron(2);
        QVERIFY(std::abs(m_server->blockEntities().machine(pos)->experience - 1.4) < 1e-9);
        send(QcMsg::QuickMoveItem{outputSlot});
        QVERIFY(m_server->blockEntities().machine(pos)->stacks[2].isEmpty());
        QCOMPARE(m_server->blockEntities().machine(pos)->experience, 0.0);
        QVERIFY(gathered() >= 1 && gathered() <= 2);
    }

    // A hopper draining the output takes no experience; the furnace keeps it while it rests (no
    // longer simulated, but kept and saved) and gives all of it to the next player taking output
    // (decision E-399).
    void aHopperDrainsTheOutputWithoutTheExperience()
    {
        join();
        const QcBlockPos pos = smeltIron(2);
        const QcBlockPos below = pos.offset(0, -1, 0);
        QVERIFY(m_server->world().setBlock(below, id("qtcraft:signal_hopper")));
        const auto furnace = [&] { return m_server->blockEntities().machine(pos); };
        // The spare coal out, so the furnace goes cold when the burning one is used up.
        move(fuelSlot, 21);
        QVERIFY(furnace()->stacks[1].isEmpty());
        for (int i = 0; i < 20 * 120 && !furnace()->isCold(); ++i) {
            ticks(1);
        }
        QVERIFY(furnace());
        QVERIFY(furnace()->isCold());
        QVERIFY(!furnace()->isIdle());
        QVERIFY(std::abs(furnace()->experience - 1.4) < 1e-9);
        QCOMPARE(m_server->blockEntities().container(below)->stacks.value(0).count, 2);
        QCOMPARE(gathered(), 0);

        // Smelting one more and taking it releases both.
        QVERIFY(m_server->world().setBlock(below, id("qtcraft:stone")));
        send(QcMsg::UseItem{pos, QcBlockFace::West});
        chat(u"/give raw_iron 1"_s);
        chat(u"/give coal 1"_s);
        move(slotOf(item("qtcraft:raw_iron")), inputSlot);
        move(slotOf(item("qtcraft:coal")), fuelSlot);
        for (int i = 0; i < 20 * 15 && furnace()->stacks[2].isEmpty(); ++i) {
            ticks(1);
        }
        QVERIFY(std::abs(furnace()->experience - 2.1) < 1e-9);
        move(outputSlot, 20);
        QCOMPARE(furnace()->experience, 0.0);
        QVERIFY(gathered() >= 2 && gathered() <= 3);
    }

    void aBrokenFurnaceReleasesItsExperience()
    {
        join();
        const QcBlockPos pos = smeltIron(2);
        QVERIFY(std::abs(m_server->blockEntities().machine(pos)->experience - 1.4) < 1e-9);
        QVERIFY(m_server->world().setBlock(pos, qcAirBlockId));
        ticks(1);
        QVERIFY(gathered() >= 1 && gathered() <= 2);
    }

    void bredAnimalsGiveExperience()
    {
        join();
        const int pig = *m_content->mobIndex(u"qtcraft:pig"_s);
        const QcVec3 place{m_feet.x + 2.5, static_cast<double>(m_feet.y), m_feet.z + 0.5};
        QList<quint32> parents;
        for (const double dz : {-0.5, 0.5}) {
            parents.append(m_server->mobs().spawn(pig, place + QcVec3{0.0, 0.0, dz}, m_server->entities().newId(), false, true));
            m_server->mobs().find(parents.last())->love = 30.0;
        }

        const auto young = [this] {
            return std::ranges::count_if(m_server->mobs().mobs(), [](const QcCreature &creature) { return creature.isYoung(); });
        };
        for (int i = 0; i < 400 && young() == 0; ++i) {
            ticks(1);
        }
        QCOMPARE(young(), 1);
        QVERIFY2(gathered() >= 1 && gathered() <= 7, qPrintable(QString::number(gathered())));
    }

    void lapisOreLiesDeepInTheGeneratedWorld()
    {
        const QcTerrainGenerator generator(m_content, 36);
        QMap<int, int> byHeight;
        qsizetype total = 0;
        for (int cx = 0; cx < 8; ++cx) {
            for (int cz = 0; cz < 8; ++cz) {
                const QList<QcBlockPos> found = lapisIn(generator, {cx, cz});
                total += found.size();
                for (const QcBlockPos &pos : found) {
                    ++byHeight[pos.y];
                }
            }
        }

        // Two veins of up to seven per chunk around heights 2 to 34 (content/qtcraft/ores/lapis.json).
        QVERIFY2(total > 64 && total < qsizetype(64) * 2 * 10, qPrintable(QString::number(total)));
        qsizetype inRange = 0;
        for (auto it = byHeight.cbegin(); it != byHeight.cend(); ++it) {
            inRange += it.key() >= 2 && it.key() <= 34 ? it.value() : 0;
        }
        QVERIFY2(inRange * 10 >= total * 9, qPrintable(u"%1 of %2"_s.arg(inRange).arg(total)));
        QVERIFY(byHeight.lastKey() <= 40);

        // The same seed places the same ore.
        const QcTerrainGenerator again(m_content, 36);
        QCOMPARE(lapisIn(again, {2, 2}), lapisIn(generator, {2, 2}));
    }

    void lapisRecipes()
    {
        const char *l = "qtcraft:lapis_lazuli";
        QCOMPARE(crafted({l, l, l, l, l, l, l, l, l}, 3), u"1 qtcraft:lapis_block"_s);
        QCOMPARE(crafted({"qtcraft:lapis_block"}, 2), u"9 qtcraft:lapis_lazuli"_s);
        QCOMPARE(crafted({l}, 2), u"1 qtcraft:blue_dye"_s);
    }
};

QTEST_GUILESS_MAIN(TestExperienceSources)
#include "test_experience_sources.moc"
