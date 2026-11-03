// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/core/QcWeightedChoice.h"

#include <QList>
#include <QTest>

#include <array>
#include <ranges>

namespace {

struct Entry
{
    int id = 0;
    int weight = 0;
};

} // namespace

Q_DECLARE_TYPEINFO(Entry, Q_PRIMITIVE_TYPE);

namespace {

struct Share
{
    int id = 0;
    double weight = 0.0;
};

// Weights 2, 0, 3, 0 (ids 1 to 4): the draws 0–1 fall on id 1, 2–4 on id 3, 5 and above on none.
constexpr std::array<Entry, 4> entries{{{1, 2}, {2, 0}, {3, 3}, {4, 0}}};
// Weight 0 in front: never chosen, the first draw goes to the next entry.
constexpr std::array<Entry, 3> zeroFirst{{{1, 0}, {2, 1}, {3, 1}}};
constexpr std::array<Entry, 2> allZero{{{1, 0}, {2, 0}}};
constexpr std::array<Share, 3> shares{{{1, 0.5}, {2, 0.0}, {3, 1.5}}};

constexpr int idAt(int pick)
{
    const auto chosen = qcWeightedFind(entries, pick, &Entry::weight);
    return chosen != entries.end() ? chosen->id : -1;
}

constexpr double nextBelowOne = 0.99999999999999989; // the largest double below 1

static_assert(qcWeightTotal(entries, &Entry::weight) == 5);
static_assert(qcWeightTotal(allZero, &Entry::weight) == 0);
static_assert(qcWeightTotal(shares, &Share::weight) == 2.0);

// A draw exactly on a border goes to the next entry with weight; zeros in the middle and at the end
// are never chosen; at or past the total the end comes back.
static_assert(idAt(0) == 1);
static_assert(idAt(1) == 1);
static_assert(idAt(2) == 3);
static_assert(idAt(4) == 3);
static_assert(idAt(5) == -1);
static_assert(idAt(100) == -1);
static_assert(qcWeightedFind(zeroFirst, 0, &Entry::weight)->id == 2);
static_assert(qcWeightedFind(zeroFirst, 1, &Entry::weight)->id == 3);
static_assert(qcWeightedFind(allZero, 0, &Entry::weight) == allZero.end());

// Doubles: a draw on a border goes on as well.
static_assert(qcWeightedFind(shares, 0.0, &Share::weight)->id == 1);
static_assert(qcWeightedFind(shares, 0.5, &Share::weight)->id == 3);
static_assert(qcWeightedFind(shares, 2.0, &Share::weight) == shares.end());

// qcWeightedPick: random scales the total; just below 1 still lands inside, a total of 0 gives the
// last entry.
static_assert(qcWeightedPick(entries, 0.0, &Entry::weight).id == 1);
static_assert(qcWeightedPick(entries, 0.4, &Entry::weight).id == 3);
static_assert(qcWeightedPick(entries, nextBelowOne, &Entry::weight).id == 3);
static_assert(qcWeightedPick(zeroFirst, 0.0, &Entry::weight).id == 2);
static_assert(qcWeightedPick(allZero, 0.5, &Entry::weight).id == 2);
static_assert(qcWeightedPick(shares, 0.25, &Share::weight).id == 3);
static_assert(qcWeightedPick(shares, nextBelowOne, &Share::weight).id == 3);
static_assert(qcWeightedPick(shares, 0.2, &Share::weight).id == 1);

} // namespace

/*! Choice by weight (QcWeightedChoice.h, decision E-977); the border cases are static_asserts above,
    the cases here use Qt containers and ranges of indices the way the callers do. */
class TestWeightedChoice : public QObject
{
    Q_OBJECT

private slots:
    void picksFromAQList();
    void indicesWithClosedEntries();
    void eachEntryAsLikelyAsItsWeight();
};

void TestWeightedChoice::picksFromAQList()
{
    const QList<Entry> list{{1, 1}, {2, 2}};
    QCOMPARE(qcWeightedPick(list, 0.0, &Entry::weight).id, 1);
    QCOMPARE(qcWeightedPick(list, 0.34, &Entry::weight).id, 2);
    QCOMPARE(qcWeightedPick(list, nextBelowOne, &Entry::weight).id, 2);
    QCOMPARE(&qcWeightedPick(list, 0.5, &Entry::weight), &list.constLast());

    const auto byLambda = [](Entry entry) { return entry.weight * 2; };
    QCOMPARE(qcWeightTotal(list, byLambda), 6);
    QVERIFY(qcWeightedFind(list, 6, byLambda) == list.cend());
}

void TestWeightedChoice::indicesWithClosedEntries()
{
    // Like QcPiecePlacer::choosePiece: a closed piece weighs nothing, the result is an index.
    const QList<int> weights{4, 1, 2, 3};
    const QList<bool> open{false, true, false, true};
    const auto weightOf = [&](qsizetype i) { return open[i] ? weights[i] : 0; };
    const auto indices = std::views::iota(qsizetype{0}, weights.size());

    QCOMPARE(qcWeightTotal(indices, weightOf), 4);
    QCOMPARE(*qcWeightedFind(indices, 0, weightOf), qsizetype{1});
    QCOMPARE(*qcWeightedFind(indices, 1, weightOf), qsizetype{3});
    QCOMPARE(*qcWeightedFind(indices, 3, weightOf), qsizetype{3});
    QVERIFY(qcWeightedFind(indices, 4, weightOf) == indices.end());
}

void TestWeightedChoice::eachEntryAsLikelyAsItsWeight()
{
    // Even steps of random over [0, 1) hit each entry in proportion to its weight.
    const QList<Entry> list{{0, 1}, {1, 0}, {2, 3}, {3, 4}};
    std::array<int, 4> hits{};
    constexpr int steps = 800;
    for (int i = 0; i < steps; ++i) {
        ++hits[static_cast<std::size_t>(qcWeightedPick(list, static_cast<double>(i) / steps, &Entry::weight).id)];
    }
    QVERIFY(hits == (std::array<int, 4>{100, 0, 300, 400}));
}

QTEST_GUILESS_MAIN(TestWeightedChoice)
#include "test_weighted_choice.moc"
