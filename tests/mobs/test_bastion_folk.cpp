// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mobs/QcGoldFolkFixture.h"
#include "server/core/QcServerWorld.h"
#include "worldgen/nether/QcNetherGenerator.h"

#include <QScopeGuard>

using namespace Qt::StringLiterals;

// The bruiser of a bastion (decision E-1089): always hostile, with an axe, no use for gold, and
// the folk that stand in a bastion from the start. Ticks are stepped by hand.
class TestBastionFolk : public QcGoldFolkFixture
{
    Q_OBJECT

private:
    // Opens a world around a chunk of a bastion that holds folk; m_resident is the first of them.
    void joinBastion()
    {
        constexpr qint64 seed = 42;
        auto generator = std::make_unique<QcNetherGenerator>(m_content, seed);
        std::shared_ptr<const QcBastionPlan> plan;
        for (int i = 0; i < 64 && !plan; ++i) {
            plan = generator->bastion().planOf(i % 8 - 4, i / 8 - 4);
        }
        QVERIFY(plan);
        const auto inChunkOf = [](QcBlockPos cell) { return [cell](QcBastionPlan::Resident other) { return other.at.chunk() == cell.chunk(); }; };
        const auto crowded = std::ranges::max_element(plan->residents, {}, [&plan, &inChunkOf](QcBastionPlan::Resident resident) { return std::ranges::count_if(plan->residents, inChunkOf(resident.at)); });
        m_resident = crowded->at;
        m_expected = static_cast<qsizetype>(std::ranges::count_if(plan->residents, inChunkOf(m_resident)));
        join({}, std::move(generator));
        m_server->mobs().setNaturalSpawning(false);
        m_server->mobs().removeAll();
    }

    // Loads the chunk of m_resident and holds everybody in it still; returns their ids.
    [[nodiscard]] QList<quint32> holdFolk()
    {
        loadAround(m_resident, 0);
        QList<quint32> folk;
        for (const QcCreature &mob : m_server->mobs().mobs()) {
            if (inChunk(mob)) {
                folk.append(mob.id);
            }
        }
        for (const quint32 id : std::as_const(folk)) {
            m_server->mobs().find(id)->noAi = true;
        }
        return folk;
    }

    [[nodiscard]] bool inChunk(const QcCreature &mob) const { return QcPhysics::blockContaining(mob.motion.position).chunk() == m_resident.chunk(); }

    [[nodiscard]] qsizetype standing() const
    {
        return std::ranges::count_if(m_server->mobs().mobs(), [this](const QcCreature &mob) { return inChunk(mob); });
    }

    [[nodiscard]] bool standsAtTheResident() const
    {
        return std::ranges::any_of(m_server->mobs().mobs(), [this](const QcCreature &mob) { return QcPhysics::blockContaining(mob.motion.position) == m_resident; });
    }

    void kill(quint32 id)
    {
        QVERIFY(m_server->entities().creatures().damage({.creature = id, .damage = 1000.0, .from = player().motion.position}));
        ticks(1);
    }

    void regenerate()
    {
        chat(u"/regenerate %1 %2"_s.arg(m_resident.x).arg(m_resident.z));
        chat(u"/confirm"_s);
        ticks(1);
    }

    QcBlockPos m_resident;
    qsizetype m_expected = 0;

    [[nodiscard]] const QcMobDefinition &definition(const char *name) const { return m_content->mobs()[*m_content->mobKind(QString::fromLatin1(name))]; }

private slots:
    void theBruiserKeepsTheRoleModelsValues()
    {
        const QcMobDefinition &bruiser = definition("qtcraft:tuskling_bruiser");
        QCOMPARE(bruiser.maxHealth, 50.0);
        QCOMPARE(bruiser.attackDamage, 7.0);
        QCOMPARE(bruiser.experience.max, 20);
        QCOMPARE(bruiser.width, 0.6);
        QCOMPARE(bruiser.height, 1.95);
        QCOMPARE(m_content->item(bruiser.heldItemId).name, u"qtcraft:golden_axe"_s);
        QVERIFY(bruiser.hostile);
        QVERIFY(!bruiser.barter);
        QCOMPARE(bruiser.spawnWeight, 0.0);
        QCOMPARE(bruiser.drops.size(), 1);
        QCOMPARE(bruiser.drops.constFirst().chance, 0.085);
    }

    // Gold on the body means nothing to it: it comes and strikes.
    void goldDoesNotSpareThePlayer()
    {
        joinPasture();
        wearGold();
        const quint32 bruiser = spawn(u"tuskling_bruiser"_s, 6.0, 0.0);
        const double before = player().health;
        ticks(100);
        QVERIFY2(player().health < before, qPrintable(QString::number(player().health)));
        QVERIFY(m_server->mobs().find(bruiser));
    }

    // The tuskling beside it spares the same player: the difference is the kind.
    void aTusklingSparesThatPlayer()
    {
        joinPasture();
        wearGold();
        spawn(u"tuskling"_s, 6.0, 0.0);
        const double before = player().health;
        ticks(100);
        QCOMPARE(player().health, before);
    }

    // Gold lying before it stays there, and the bar in the hand buys nothing.
    void itTakesNoGold()
    {
        joinPasture(u"creative"_s);
        const quint32 bruiser = spawn(u"tuskling_bruiser"_s, 3.0, 0.0);
        lay(QcItemStack{item("qtcraft:gold_ingot"), 1, 0}, 3.0, 1.0);
        ticks(60);
        QCOMPARE(lyingAround().value(u"qtcraft:gold_ingot"_s), 1);
        QVERIFY(!mob(bruiser).barter);
    }

    // Those of a bastion stand there when its chunks first arrive and are kept with the world: the
    // chunk leaves and comes back with the living ones, and the killed one stays away.
    void aBastionIsPeopledOnce()
    {
        m_unloadChunks = true;
        const QScopeGuard keepChunksAgain([this] { m_unloadChunks = false; });
        joinBastion();
        const QList<quint32> folk = holdFolk();
        QVERIFY(!folk.isEmpty());
        QCOMPARE(folk.size(), m_expected);
        const auto here = std::ranges::find_if(m_server->mobs().mobs(), [this](const QcCreature &mob) { return QcPhysics::blockContaining(mob.motion.position) == m_resident; });
        QVERIFY(here != m_server->mobs().mobs().end());
        QVERIFY(here->persistent);
        QVERIFY(m_server->world().blockEntityExtras().isFurnished(m_resident.chunk()));

        // Nobody is near the chunk, so it leaves with the next ticks and takes the living along.
        kill(here->id);
        ticks(200);
        QVERIFY(!m_server->world().chunk(m_resident.chunk()));
        QCOMPARE(standing(), 0);

        // Back with a player, or the chunk leaves again at once.
        chat(u"/tp %1 %2 %3"_s.arg(m_resident.x).arg(m_resident.y).arg(m_resident.z));
        loadAround(m_resident, 0);
        ticks(1);
        QCOMPARE(standing(), m_expected - 1);
        QVERIFY(!standsAtTheResident());
    }

    // Regenerating gives a bastion's blocks back, not its creatures: nobody doubles and the killed
    // one stays dead.
    void regeneratingBringsNobodyBack()
    {
        joinBastion();
        QCOMPARE(holdFolk().size(), m_expected);
        regenerate();
        regenerate();
        QCOMPARE(standing(), m_expected);

        const auto here = std::ranges::find_if(m_server->mobs().mobs(), [this](const QcCreature &mob) { return QcPhysics::blockContaining(mob.motion.position) == m_resident; });
        QVERIFY(here != m_server->mobs().mobs().end());
        kill(here->id);
        regenerate();
        QCOMPARE(standing(), m_expected - 1);
        QVERIFY(!standsAtTheResident());
    }

    // The command goes through the same word as the generator and answers with its place.
    void locateNamesTheBastion()
    {
        join({}, std::make_unique<QcNetherGenerator>(m_content, 42));
        const QcBlockPos feet = QcPhysics::blockContaining(player().motion.position);
        const QcLocateResult expected = m_server->world().generator().locate(u"bastion", feet, QcVillageCommands::searchRadius);
        QVERIFY(expected.place);
        m_client->chatLines.clear();
        chat(u"/locate bastion"_s);
        ticks(2);
        QVERIFY(!m_client->chatLines.isEmpty());
        const QString line = m_client->chatLines.constLast();
        const QcBlockPos at = expected.place->at;
        const QString place = u"Bastion at %1 %2 %3,"_s.arg(at.x).arg(at.y).arg(at.z);
        QVERIFY2(line.contains(place), qPrintable(line));
    }

    // Struck down by the player, it leaves its axe at the chance of the data, and nothing else.
    void itDropsItsAxeAtTheChance()
    {
        joinPasture();
        constexpr int kills = 400;
        for (int i = 0; i < kills; ++i) {
            const quint32 bruiser = spawn(u"tuskling_bruiser"_s, 2.0, 2.0);
            QcCreature *mob = m_server->mobs().find(bruiser);
            mob->noAi = true;
            mob->health = 1.0;
            player().attackCooldown = 0.0;
            send(QcMsg::AttackEntity{bruiser});
            ticks(3);
            QVERIFY(!m_server->mobs().find(bruiser));
        }

        const QHash<QString, int> left = lyingAround();
        QVERIFY(left.size() <= 1);
        const int axes = left.value(u"qtcraft:golden_axe"_s);
        // 8.5 % of 400 is 34, the standard deviation 5.6.
        QVERIFY2(axes >= 10 && axes <= 70, qPrintable(QString::number(axes)));
    }
};

QTEST_GUILESS_MAIN(TestBastionFolk)
#include "test_bastion_folk.moc"
