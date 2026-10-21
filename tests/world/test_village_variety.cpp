#include "QcTestUtil.h"
#include "world/QcVillageTestUtil.h"
#include "worldgen/terrain/QcNoise.h"

#include <QTest>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;
using namespace QcVillageTestUtil;

// Variety of villages (decision E-404): several blueprints per kind of building, choices and optional
// parts drawn per building from the seed, and kinds with rules (animal pen with a chance, fisher's
// hut with a jetty only near water, watchtower only in large villages), all a pure function of the
// seed like the rest of a village.
class TestVillageVariety : public QObject
{
    Q_OBJECT

private:
    static constexpr qint64 seed = 42;
    // Regions around the origin the statistics run over (25 x 25).
    static constexpr int rings = 13;

    static QList<const QcStructurePlacement *> piecesNamed(const QcVillagePlan &plan, const QString &part)
    {
        QList<const QcStructurePlacement *> pieces;
        for (const QcStructurePlacement &piece : plan.pieces) {
            if (shortName(piece).contains(part)) {
                pieces.append(&piece);
            }
        }
        return pieces;
    }

    // Cells of one piece as a list of states, for comparing two copies.
    static QList<QcBlockId> cellsOf(const QcStructurePlacement &piece)
    {
        const QcStructure &structure = piece.structure();
        QList<QcBlockId> cells;
        for (int y = 0; y < structure.sizeY(); ++y) {
            for (int z = 0; z < structure.sizeZ(); ++z) {
                for (int x = 0; x < structure.sizeX(); ++x) {
                    cells.append(structure.block(x, y, z).value_or(QcBlockId(0xffff)));
                }
            }
        }
        return cells;
    }

    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    QcTerrainGenerator m_generator{m_content, seed};
    // A fixed sample for the shares (decision E-424): the first 96 villages ring by ring.
    static constexpr qsizetype sampleSize = 96;
    QList<std::shared_ptr<const QcVillagePlan>> m_plans = samplePlans(m_generator, sampleSize, rings);

    // The first plan on land (over water test_village_swamp checks the pieces) with a piece whose
    // name contains `part`, null without one.
    std::shared_ptr<const QcVillagePlan> planWith(const QString &part) const
    {
        for (const std::shared_ptr<const QcVillagePlan> &plan : m_plans) {
            if (plan->stiltPost == qcAirBlockId && !piecesNamed(*plan, part).isEmpty()) {
                return plan;
            }
        }
        return nullptr;
    }

    // Generates every chunk of the plan in order and backwards on four threads with a fresh
    // generator, compares them and checks that pieces and jetties are built and stand on the ground.
    void checkBuiltInAnyOrder(const QcVillagePlan &plan) const
    {
        const QList<QcChunkPos> positions = chunksOf(plan);
        const Chunks first = generateInOrder(m_generator, positions);
        const QcTerrainGenerator fresh(m_content, seed);
        const Chunks second = generateOnThreads(fresh, positions);
        for (const QcChunkPos &pos : positions) {
            QVERIFY2(sameBlocks(*first.value(pos), *second.value(pos)), qPrintable(u"chunk %1 %2 differs"_s.arg(pos.x).arg(pos.z)));
        }
        for (const QcStructurePlacement &piece : plan.pieces) {
            QVERIFY2(pieceIsBuilt(first, piece), qPrintable(shortName(piece)));
            QVERIFY2(standsOnGround(*m_content, first, piece), qPrintable(shortName(piece)));
        }
        for (const QcVillagePlan::Deck &deck : plan.decks) {
            QCOMPARE(blockIn(first, deck.at), deck.block);
        }
    }

private slots:
    void blueprintsSpreadOverTheVariants()
    {
        QCOMPARE(m_plans.size(), sampleSize);
        QHash<QString, int> counts;
        for (const std::shared_ptr<const QcVillagePlan> &plan : std::as_const(m_plans)) {
            for (const QcStructurePlacement &piece : plan->pieces) {
                ++counts[shortName(piece)];
            }
        }

        // Every house blueprint of both styles is built somewhere, none takes almost all.
        for (const char *style : {"plains", "desert"}) {
            for (const char *kind : {"small", "large"}) {
                const QString prefix = u"village_%1_house_%2"_s.arg(QLatin1StringView(style), QLatin1StringView(kind));
                const QStringList names = QString::fromLatin1(kind) == u"small"_s ? QStringList{prefix, prefix + u"_b"_s, prefix + u"_c"_s}
                                                                                : QStringList{prefix, prefix + u"_b"_s};
                int total = 0;
                for (const QString &name : names) {
                    QVERIFY2(counts.value(name) > 0, qPrintable(name));
                    total += counts.value(name);
                }
                for (const QString &name : names) {
                    QVERIFY2(counts.value(name) < total * 3 / 4, qPrintable(name));
                }
            }
        }
    }

    void variationIsDeterministicAndSeedDependent()
    {
        // Two generators of one seed build every piece cell for cell alike.
        const QcTerrainGenerator again(m_content, seed);
        const QList<std::shared_ptr<const QcVillagePlan>> second = plansAround(again, 3);
        const QList<std::shared_ptr<const QcVillagePlan>> first = plansAround(m_generator, 3);
        QCOMPARE(first.size(), second.size());
        for (qsizetype i = 0; i < first.size(); ++i) {
            QCOMPARE(first[i]->pieces.size(), second[i]->pieces.size());
            for (qsizetype p = 0; p < first[i]->pieces.size(); ++p) {
                QCOMPARE(cellsOf(first[i]->pieces[p]), cellsOf(second[i]->pieces[p]));
                QCOMPARE(first[i]->pieces[p].origin(), second[i]->pieces[p].origin());
            }
            QCOMPARE(first[i]->decks.size(), second[i]->decks.size());
            QCOMPARE(first[i]->mobs.size(), second[i]->mobs.size());
        }

        // Copies of one blueprint differ among each other.
        QHash<QString, QSet<QList<QcBlockId>>> looks;
        for (const std::shared_ptr<const QcVillagePlan> &plan : std::as_const(m_plans)) {
            for (const QcStructurePlacement &piece : plan->pieces) {
                looks[shortName(piece)].insert(cellsOf(piece));
            }
        }
        QVERIFY(looks.value(u"village_plains_house_small"_s).size() >= 6);
        QVERIFY(looks.value(u"village_desert_house_small_b"_s).size() >= 4);

        // Another seed draws other variants for the same blueprint: the first plains small houses
        // of both seeds differ in at least one of the first few copies.
        const QcTerrainGenerator other(m_content, seed + 1);
        QList<QList<QcBlockId>> mine;
        QList<QList<QcBlockId>> theirs;
        const auto collect = [](const QcTerrainGenerator &generator, QList<QList<QcBlockId>> &list) {
            const QList<std::shared_ptr<const QcVillagePlan>> plans = plansAround(generator, 3);
            for (const std::shared_ptr<const QcVillagePlan> &plan : plans) {
                const QList<const QcStructurePlacement *> houses = piecesNamed(*plan, u"village_plains_house_small_b"_s);
                for (const QcStructurePlacement *piece : houses) {
                    list.append(cellsOf(*piece));
                }
            }
        };
        collect(m_generator, mine);
        collect(other, theirs);
        QVERIFY(!mine.isEmpty() && !theirs.isEmpty());
        QVERIFY(mine != theirs);
    }

    void optionalPartsAppearWithTheirChance()
    {
        // Drawn many times, every part comes with about its chance and every option of every choice
        // about equally often.
        const QcVillageStyles styles(*m_content);
        const QcVillageStyle *plains = styles.named(u"qtcraft:plains"_s);
        QVERIFY(plains);
        const QcPieceVariation &variation = plains->pieces.front().blueprints[1].variation;
        const QList<double> chances = variation.partChances();
        const QList<int> counts = variation.optionCounts();
        QVERIFY(chances.size() >= 2);
        QVERIFY(counts.size() >= 3);

        constexpr int draws = 4000;
        QList<int> built(chances.size(), 0);
        QList<QList<int>> taken;
        for (const int count : counts) {
            taken.append(QList<int>(count, 0));
        }
        int next = 0;
        const auto random = [&next] { return qcPositionRandom(1234, 0, next++, 0); };
        for (int i = 0; i < draws; ++i) {
            const QcPieceVariation::Selection selection = variation.choose(random);
            for (qsizetype p = 0; p < chances.size(); ++p) {
                built[p] += selection.parts[p] ? 1 : 0;
            }
            for (qsizetype c = 0; c < counts.size(); ++c) {
                ++taken[c][selection.options[c]];
            }
        }
        for (qsizetype p = 0; p < chances.size(); ++p) {
            QVERIFY2(std::abs(double(built[p]) / draws - chances[p]) < 0.04, qPrintable(QString::number(built[p])));
        }
        for (qsizetype c = 0; c < counts.size(); ++c) {
            for (const int n : std::as_const(taken[c])) {
                QVERIFY(std::abs(double(n) / draws - 1.0 / counts[c]) < 0.04);
            }
        }

        // In the villages, a part (the chimney) is there in some copies and missing in others.
        const QcBlockId cobblestone = *m_content->stateId(u"qtcraft:cobblestone"_s);
        int with = 0;
        int without = 0;
        for (const std::shared_ptr<const QcVillagePlan> &plan : std::as_const(m_plans)) {
            const QList<const QcStructurePlacement *> houses = piecesNamed(*plan, u"village_plains_house_small_b"_s);
            for (const QcStructurePlacement *piece : houses) {
                const bool chimney = piece->structure().block(2, 7, 1) == cobblestone;
                with += chimney ? 1 : 0;
                without += chimney ? 0 : 1;
            }
        }
        QVERIFY2(with > 0 && without > 0, qPrintable(u"%1 with, %2 without"_s.arg(with).arg(without)));
    }

    void pensComeWithTheirChanceAndOnePair()
    {
        int villages = 0;
        int pens = 0;
        int cats = 0;
        for (const std::shared_ptr<const QcVillagePlan> &plan : std::as_const(m_plans)) {
            const QList<const QcStructurePlacement *> found = piecesNamed(*plan, u"_pen"_s);
            QVERIFY(found.size() <= 1);
            ++villages;
            pens += int(found.size());
            // The creatures of a village are its pen's pair, two of one kind inside the fence, and a cat in
            // each large house (decision E-480).
            QList<QcVillagePlan::Mob> pair;
            std::ranges::copy_if(plan->mobs, std::back_inserter(pair), [](const QcVillagePlan::Mob &mob) { return mob.mob != u"qtcraft:cat"_s; });
            QCOMPARE(pair.size(), 2 * found.size());
            // Every large house with a cat in its structure (the first blueprint of each style) brings it.
            qsizetype catHouses = 0;
            const QList<const QcStructurePlacement *> houses = piecesNamed(*plan, u"_house_large"_s);
            for (const QcStructurePlacement *house : houses) {
                catHouses += std::ranges::count(house->structure().mobs, u"qtcraft:cat"_s, &QcStructure::Mob::mob);
            }
            QCOMPARE(plan->mobs.size() - pair.size(), catHouses);
            cats += int(catHouses);
            if (found.isEmpty()) {
                continue;
            }
            const QcStructurePlacement &pen = *found.front();
            QCOMPARE(pair[0].mob, pair[1].mob);
            QVERIFY(pair[0].mob == u"qtcraft:pig"_s || pair[0].mob == u"qtcraft:sheep"_s);
            for (const QcVillagePlan::Mob &mob : std::as_const(pair)) {
                QVERIFY(mob.at.x > pen.origin().x + 1 && mob.at.x < pen.origin().x + pen.sizeX() - 1);
                QVERIFY(mob.at.z > pen.origin().z + 1 && mob.at.z < pen.origin().z + pen.sizeZ() - 1);
                QCOMPARE(mob.at.y, pen.origin().y + 1.0);
            }
        }
        // Chances 0.5 (plains, taiga), 0.4 (desert) and 0.3 (snowy), less where no spot fits, none in
        // swamp villages: 37 of the 96 villages of the sample at seed 42 (0.39). One standard
        // deviation of the share among 96 villages is about 0.05; the bounds lie three to four of
        // them around it.
        const double share = double(pens) / villages;
        qInfo("%d pens and %d cats in %d villages", pens, cats, villages);
        QVERIFY(cats > 0);
        QVERIFY2(share > 0.2 && share < 0.6, qPrintable(u"%1 pens in %2 villages"_s.arg(pens).arg(villages)));
    }

    void fishersHutsStandNearWaterWithAJetty()
    {
        int huts = 0;
        for (const std::shared_ptr<const QcVillagePlan> &plan : std::as_const(m_plans)) {
            // Over water a fisher's hut needs no jetty: the boardwalks are decks (test_village_swamp).
            if (plan->stiltPost != qcAirBlockId) {
                continue;
            }
            const QList<const QcStructurePlacement *> found = piecesNamed(*plan, u"fisher_hut"_s);
            QVERIFY(found.size() <= 1);
            // A jetty only with a hut.
            QCOMPARE(plan->decks.isEmpty(), found.isEmpty());
            if (found.isEmpty()) {
                continue;
            }
            ++huts;
            // The jetty starts beside the hut (within a few blocks) and its far end lies over water.
            const QcBlockPos far = plan->decks.back().at;
            QVERIFY(m_generator.column(far.x, far.z).height - 1 < m_generator.seaLevel());
            const QcStructurePlacement &hut = *found.front();
            const QcBlockPos near = plan->decks.front().at;
            const int gapX = std::max({0, hut.origin().x - near.x, near.x - (hut.origin().x + hut.sizeX() - 1)});
            const int gapZ = std::max({0, hut.origin().z - near.z, near.z - (hut.origin().z + hut.sizeZ() - 1)});
            QVERIFY2(gapX + gapZ <= 12, qPrintable(u"jetty %1 blocks from its hut"_s.arg(gapX + gapZ)));
        }
        QVERIFY2(huts >= 2, qPrintable(u"%1 huts"_s.arg(huts)));

        // Built: the jetty's far end stands in the water on posts reaching the ground (a village on
        // land; over water the decks are boardwalks).
        const auto onLand = std::ranges::find_if(std::as_const(m_plans), [](const std::shared_ptr<const QcVillagePlan> &candidate) {
            return candidate->stiltPost == qcAirBlockId && !piecesNamed(*candidate, u"fisher_hut"_s).isEmpty();
        });
        QVERIFY(onLand != std::as_const(m_plans).end());
        const std::shared_ptr<const QcVillagePlan> &plan = *onLand;
        QVERIFY(plan);
        QList<QcChunkPos> positions;
        for (const QcVillagePlan::Deck &deck : plan->decks) {
            if (!positions.contains(deck.at.chunk())) {
                positions.append(deck.at.chunk());
            }
        }
        const Chunks chunks = generateInOrder(m_generator, positions);
        // Water, or water frozen over (the jetty of a tundra village stands on ice).
        const QcBlockId ice = m_content->blockId(u"qtcraft:ice"_s).value_or(qcAirBlockId);
        const auto isWater = [&](QcBlockPos at) {
            const QcBlockId block = *blockIn(chunks, at);
            return m_content->block(block).liquid || block == ice;
        };
        bool touches = false;
        for (const QcVillagePlan::Deck &deck : plan->decks) {
            QCOMPARE(blockIn(chunks, deck.at), deck.block);
            const QcBlockPos below = deck.at.offset(0, -1, 0);
            touches = touches || isWater(below) || isWater(below.offset(0, -1, 0));
            if (deck.post == qcAirBlockId) {
                continue;
            }
            int y = deck.at.y - 1;
            while (blockIn(chunks, {deck.at.x, y, deck.at.z}) == deck.post) {
                --y;
            }
            QVERIFY2(m_content->block(*blockIn(chunks, {deck.at.x, y, deck.at.z})).solid, "a post stands on solid ground");
        }
        QVERIFY(touches);
    }

    void towersStandOnlyInLargeVillages()
    {
        int towers = 0;
        int large = 0;
        for (const std::shared_ptr<const QcVillagePlan> &plan : std::as_const(m_plans)) {
            const qsizetype found = piecesNamed(*plan, u"_tower"_s).size();
            QVERIFY(found <= 1);
            towers += int(found);
            // The base game's styles build towers in villages whose roads are 200 blocks long
            // together (decision E-408).
            large += plan->roadLength >= 200 ? 1 : 0;
            QVERIFY2(found == 0 || plan->roadLength >= 200, qPrintable(u"tower with %1 blocks of road"_s.arg(plan->roadLength)));
        }
        // Most large villages find room for theirs (at least half), and large villages are rare: at
        // seed 42 about one in ten of the sample, at most one in five allowed.
        QVERIFY2(towers >= 1 && towers * 2 >= large, qPrintable(u"%1 towers in %2 large villages"_s.arg(towers).arg(large)));
        QVERIFY2(qsizetype(towers) * 5 <= m_plans.size(), qPrintable(u"%1 towers in %2 villages"_s.arg(towers).arg(m_plans.size())));
    }

    void newPiecesComeOutTheSameInAnyOrder()
    {
        for (const QString &part : {u"fisher_hut"_s, u"_pen"_s, u"_tower"_s}) {
            const std::shared_ptr<const QcVillagePlan> plan = planWith(part);
            QVERIFY2(plan, qPrintable(part));
            checkBuiltInAnyOrder(*plan);
        }
    }
};

QTEST_GUILESS_MAIN(TestVillageVariety)
#include "test_village_variety.moc"
