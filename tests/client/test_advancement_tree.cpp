// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/player/QcAdvancementTree.h"
#include "clientcore/player/QcTidyTreeLayout.h"
#include "QcTestUtil.h"

#include <QRandomGenerator>
#include <QTest>

using namespace Qt::StringLiterals;

// The tree view of the adventure guide without the GUI (decision E-924): the tidy layout (one column
// per depth, never two nodes in one cell, parents between their children, the same tree gives the
// same places, wide and deep trees), the rule for hidden advancements and the state of each node.
class TestAdvancementTree : public QObject
{
    Q_OBJECT

private:
    // Checks the rules of the layout for the tree below `root` of `parents`.
    static void checkLayout(const QList<qsizetype> &parents, qsizetype root)
    {
        const QcTidyTreeLayout::Layout layout = QcTidyTreeLayout::layout(parents, root);
        QHash<qsizetype, QcTidyTreeLayout::Place> placeOf;
        double lowest = layout.rows;
        for (const QcTidyTreeLayout::Place &place : layout.places) {
            placeOf.insert(place.node, place);
            lowest = std::min(lowest, place.row);
            QVERIFY(place.row + 1.0 <= layout.rows + 1e-9);
            QVERIFY(place.column < layout.columns);
        }
        QCOMPARE(lowest, 0.0);
        QCOMPARE(layout.places.constFirst().node, root);
        QCOMPARE(layout.places.constFirst().column, 0);

        // At least one row between any two nodes of a column.
        for (qsizetype a = 0; a < layout.places.size(); ++a) {
            for (qsizetype b = a + 1; b < layout.places.size(); ++b) {
                const auto &first = layout.places[a];
                const auto &second = layout.places[b];
                if (first.column == second.column && std::abs(first.row - second.row) < 1.0 - 1e-9) {
                    QFAIL(qPrintable(u"nodes %1 and %2 share column %3 at rows %4 and %5"_s.arg(first.node).arg(second.node).arg(first.column)
                                         .arg(first.row)
                                         .arg(second.row)));
                }
            }
        }
        // Children one column right of their parent, the parent halfway between the first and the last.
        for (const QcTidyTreeLayout::Place &place : layout.places) {
            QList<double> rows;
            for (qsizetype child = 0; child < parents.size(); ++child) {
                if (parents[child] == place.node && placeOf.contains(child)) {
                    QCOMPARE(placeOf[child].column, place.column + 1);
                    rows.append(placeOf[child].row);
                }
            }
            if (!rows.isEmpty()) {
                QCOMPARE(place.row, (rows.constFirst() + rows.constLast()) / 2.0);
                QVERIFY(std::ranges::is_sorted(rows));
            }
        }
    }

    // A random tree of `count` nodes: each node's parent is an earlier node (node 0 the root).
    static QList<qsizetype> randomTree(qint64 seed, qsizetype count)
    {
        QRandomGenerator random(static_cast<quint32>(seed));
        QList<qsizetype> parents{-1};
        for (qsizetype node = 1; node < count; ++node) {
            // Mostly near the end, so the trees get deep as well as wide.
            const qsizetype span = std::min<qsizetype>(node, 1 + static_cast<qsizetype>(random.bounded(8)));
            parents.append(node - 1 - static_cast<qsizetype>(random.bounded(static_cast<int>(span))));
        }
        return parents;
    }

    static QcAdvancementDefinition advancement(const QString &name, const QString &parent, bool hidden = false, int groups = 1)
    {
        QcAdvancementDefinition definition;
        definition.name = name;
        definition.parent = parent;
        definition.icon = u"qtcraft:stick"_s;
        definition.hidden = hidden;
        for (int group = 0; group < groups; ++group) {
            const QString criterion = u"c%1"_s.arg(group);
            definition.criteria.append({criterion, QcAdvancementTrigger::Impossible, {}});
            definition.requirements.append({criterion});
        }
        return definition;
    }

    // R (root) with A (hidden) -> B -> C (hidden), and D beside A.
    static QList<QcAdvancementDefinition> hiddenTree()
    {
        return {advancement(u"t:r"_s, {}), advancement(u"t:a"_s, u"t:r"_s, true), advancement(u"t:b"_s, u"t:a"_s), advancement(u"t:c"_s, u"t:b"_s, true),
                advancement(u"t:d"_s, u"t:r"_s)};
    }

    static QStringList shownNames(const QList<QcAdvancementDefinition> &advancements, const QcAdvancementProgress &progress)
    {
        const QList<bool> shown = QcAdvancementTree::visible(advancements, progress);
        QStringList names;
        for (qsizetype index = 0; index < advancements.size(); ++index) {
            if (shown[index]) {
                names.append(advancements[index].name.mid(2));
            }
        }
        return names;
    }

private slots:
    void aChainIsOneRow()
    {
        const QcTidyTreeLayout::Layout layout = QcTidyTreeLayout::layout({-1, 0, 1, 2}, 0);
        QCOMPARE(layout.columns, 4);
        QCOMPARE(layout.rows, 1.0);
        for (const QcTidyTreeLayout::Place &place : layout.places) {
            QCOMPARE(place.row, 0.0);
            QCOMPARE(place.column, static_cast<int>(place.node));
        }
    }

    void aShallowLeafMovesUpBesideADeepBranch()
    {
        // Root 0 with x (1) and y (2); x has three leaves (3, 4, 5). y only needs one row below x,
        // not a row below x's leaves.
        const QList<qsizetype> parents{-1, 0, 0, 1, 1, 1};
        const QcTidyTreeLayout::Layout layout = QcTidyTreeLayout::layout(parents, 0);
        QCOMPARE(layout.rows, 3.0);
        QHash<qsizetype, double> row;
        for (const auto &place : layout.places) {
            row.insert(place.node, place.row);
        }
        QCOMPARE(row[1], 1.0);
        QCOMPARE(row[2], 2.0);
        QCOMPARE(row[0], 1.5);
        checkLayout(parents, 0);
    }

    void aWideTreeTakesOneRowPerLeaf()
    {
        QList<qsizetype> parents{-1};
        for (int child = 0; child < 300; ++child) {
            parents.append(0);
        }
        const QcTidyTreeLayout::Layout layout = QcTidyTreeLayout::layout(parents, 0);
        QCOMPARE(layout.rows, 300.0);
        QCOMPARE(layout.columns, 2);
        QCOMPARE(layout.places.constFirst().row, 149.5);
        checkLayout(parents, 0);
    }

    // The content's limit as one chain and as one fan: one row, and one row per leaf.
    void theLimitAsAChainAndAsAFan()
    {
        constexpr qsizetype limit = 1024;
        QList<qsizetype> chain{-1};
        QList<qsizetype> fan{-1};
        for (qsizetype node = 1; node < limit; ++node) {
            chain.append(node - 1);
            fan.append(0);
        }

        const QcTidyTreeLayout::Layout deep = QcTidyTreeLayout::layout(chain, 0);
        QCOMPARE(deep.places.size(), limit);
        QCOMPARE(deep.columns, static_cast<int>(limit));
        QCOMPARE(deep.rows, 1.0);
        checkLayout(chain, 0);

        const QcTidyTreeLayout::Layout wide = QcTidyTreeLayout::layout(fan, 0);
        QCOMPARE(wide.places.size(), limit);
        QCOMPARE(wide.columns, 2);
        QCOMPARE(wide.rows, static_cast<double>(limit - 1));
        QCOMPARE(wide.places.constFirst().row, (static_cast<double>(limit) - 2.0) / 2.0);
        checkLayout(fan, 0);
    }

    void randomTreesKeepTheRules_data()
    {
        QTest::addColumn<qint64>("seed");
        QTest::addColumn<int>("count");
        for (const qint64 seed : {1, 2, 3, 42, 1234}) {
            QTest::addRow("seed %lld", static_cast<long long>(seed)) << seed << 60;
        }
        QTest::addRow("random") << QcTestUtil::randomSeed() << 200;
        QTest::addRow("the content's limit") << qint64{7} << 1024;
    }

    void randomTreesKeepTheRules()
    {
        QFETCH(qint64, seed);
        QFETCH(int, count);
        const QList<qsizetype> parents = randomTree(seed, count);
        checkLayout(parents, 0);
        QCOMPARE(QcTidyTreeLayout::layout(parents, 0).places.size(), qsizetype{count});
    }

    void theSameTreeGivesTheSamePlaces()
    {
        const QList<qsizetype> parents = randomTree(99, 120);
        const QcTidyTreeLayout::Layout first = QcTidyTreeLayout::layout(parents, 0);
        const QcTidyTreeLayout::Layout second = QcTidyTreeLayout::layout(parents, 0);
        QCOMPARE(first.places.size(), second.places.size());
        for (qsizetype index = 0; index < first.places.size(); ++index) {
            QCOMPARE(first.places[index].node, second.places[index].node);
            QCOMPARE(first.places[index].row, second.places[index].row);
            QCOMPARE(first.places[index].column, second.places[index].column);
        }
    }

    void loopsAndBadParentsEndTheWalk()
    {
        // 1 and 2 hang below each other, 3 names a parent that does not exist, 0 names itself.
        const QcTidyTreeLayout::Layout layout = QcTidyTreeLayout::layout({0, 2, 1, 17}, 1);
        QCOMPARE(layout.places.size(), qsizetype{2});
        QVERIFY(QcTidyTreeLayout::layout({-1}, 5).places.isEmpty());
        QCOMPARE(QcTidyTreeLayout::layout({0}, 0).places.size(), qsizetype{1});
    }

    void hiddenAdvancementsWaitUntilReached()
    {
        const QList<QcAdvancementDefinition> tree = hiddenTree();
        QcAdvancementProgress progress;
        QCOMPARE(shownNames(tree, progress), (QStringList{u"r"_s, u"d"_s}));

        // A reached: it shows, and the not hidden B below it with it; the hidden C still waits.
        progress.grant(u"t:a"_s, u"c0"_s);
        QCOMPARE(shownNames(tree, progress), (QStringList{u"r"_s, u"a"_s, u"b"_s, u"d"_s}));

        // Only C reached: it shows with all its ancestors, so the lines stay connected.
        progress = {};
        progress.grant(u"t:c"_s, u"c0"_s);
        QCOMPARE(shownNames(tree, progress), (QStringList{u"r"_s, u"a"_s, u"b"_s, u"c"_s, u"d"_s}));

        // Met criteria alone (not reached) show nothing hidden.
        QList<QcAdvancementDefinition> twoGroups = tree;
        twoGroups[1] = advancement(u"t:a"_s, u"t:r"_s, true, 2);
        progress = {};
        progress.grant(u"t:a"_s, u"c0"_s);
        QCOMPARE(shownNames(twoGroups, progress), (QStringList{u"r"_s, u"d"_s}));
    }

    void aHiddenRootHasNoTreeUntilReached()
    {
        const QList<QcAdvancementDefinition> advancements{advancement(u"t:r"_s, {}), advancement(u"t:secret"_s, {}, true),
                                                          advancement(u"t:inside"_s, u"t:secret"_s)};
        QcAdvancementProgress progress;
        QCOMPARE(QcAdvancementTree::trees(advancements, progress).size(), qsizetype{1});
        progress.grant(u"t:secret"_s, u"c0"_s);
        const QList<QcAdvancementTree::Tree> trees = QcAdvancementTree::trees(advancements, progress);
        QCOMPARE(trees.size(), qsizetype{2});
        QCOMPARE(trees[1].root, qsizetype{1});
        QCOMPARE(trees[1].nodes.size(), qsizetype{2});
    }

    void hiddenOnesLeaveTheLayout()
    {
        const QList<QcAdvancementDefinition> tree = hiddenTree();
        const QList<QcAdvancementTree::Tree> trees = QcAdvancementTree::trees(tree, {});
        QCOMPARE(trees.size(), qsizetype{1});
        // R and D only: D takes the row next to R, nothing waits in A's place.
        QCOMPARE(trees[0].nodes.size(), qsizetype{2});
        QCOMPARE(trees[0].rows, 1.0);
        QCOMPARE(trees[0].columns, 2);
        QCOMPARE(trees[0].nodes[1].advancement, qsizetype{4});
        QCOMPARE(trees[0].nodes[1].parent, qsizetype{0});
    }

    void statesCountTheRequirementGroups()
    {
        const QList<QcAdvancementDefinition> advancements{advancement(u"t:r"_s, {}), advancement(u"t:four"_s, u"t:r"_s, false, 4)};
        QcAdvancementProgress progress;
        progress.grant(u"t:r"_s, u"c0"_s);
        progress.grant(u"t:four"_s, u"c1"_s);
        progress.grant(u"t:four"_s, u"c3"_s);
        const QList<QcAdvancementTree::Tree> partly = QcAdvancementTree::trees(advancements, progress);
        QCOMPARE(partly.constFirst().nodes.constFirst().state, QcAdvancementTree::State::Reached);
        const QcAdvancementTree::Node four = partly.constFirst().nodes.constLast();
        QCOMPARE(four.state, QcAdvancementTree::State::Partial);
        QCOMPARE(four.groupsMet, 2);
        QCOMPARE(four.groups, 4);

        progress.grant(u"t:four"_s, u"c0"_s);
        progress.grant(u"t:four"_s, u"c2"_s);
        const QList<QcAdvancementTree::Tree> reached = QcAdvancementTree::trees(advancements, progress);
        QCOMPARE(reached.constFirst().nodes.constLast().state, QcAdvancementTree::State::Reached);
        const QList<QcAdvancementTree::Tree> open = QcAdvancementTree::trees(advancements, {});
        QCOMPARE(open.constFirst().nodes.constLast().state, QcAdvancementTree::State::Open);
    }

    void theBaseTreeIsOneTidyTree()
    {
        const std::shared_ptr<const QcContentRegistry> content = QcTestUtil::loadContent();
        const QList<QcAdvancementDefinition> &advancements = content->advancements().items();
        const QList<QcAdvancementTree::Tree> trees = QcAdvancementTree::trees(advancements, {});
        QCOMPARE(trees.size(), qsizetype{1});
        QCOMPARE(advancements[trees[0].root].name, u"qtcraft:adventure"_s);
        QCOMPARE(trees[0].nodes.size(), advancements.size());
        checkLayout(QcAdvancementTree::parentIndices(advancements), trees[0].root);
        qInfo("base tree: %d columns, %.1f rows", trees[0].columns, trees[0].rows);
    }
};

QTEST_GUILESS_MAIN(TestAdvancementTree)
#include "test_advancement_tree.moc"
