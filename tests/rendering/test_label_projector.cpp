// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/camera/QcLabelProjector.h"
#include "fixtures/QcTestWorld.h"

#include <QTest>

#include <array>

using namespace Qt::StringLiterals;

namespace {

constexpr double width = 1000.0;
constexpr double height = 500.0;

// A camera at (0.5, 65.6, 0.5) looking north (-z), level.
QcLabelProjector::View northView()
{
    QcCamera camera;
    camera.position = {0.5, 65.6, 0.5};
    camera.yaw = 0.0f;
    camera.pitch = 0.0f;
    return {camera, width, height};
}

QHash<QcChunkPos, QList<QcWorldLabel>> labelsAt(const QList<QcWorldLabel> &labels)
{
    QHash<QcChunkPos, QList<QcWorldLabel>> byChunk;
    for (const QcWorldLabel &label : labels) {
        byChunk[QcBlockPos{static_cast<int>(std::floor(label.position.x)), 0, static_cast<int>(std::floor(label.position.z))}.chunk()].append(label);
    }
    return byChunk;
}

QcWorldLabel label(const QcVec3 &at, const QString &text, bool heading = false)
{
    return {at, text, {}, heading};
}

} // namespace

class TestLabelProjector : public QObject
{
    Q_OBJECT

private slots:
    void labelStraightAheadIsInTheMiddle()
    {
        const QList<QcScreenLabel> shown = QcLabelProjector::visible(labelsAt({label({0.5, 65.6, -5.5}, u"a"_s)}), northView(), nullptr);
        QCOMPARE(shown.size(), 1);
        QCOMPARE(shown[0].text, u"a"_s);
        QVERIFY(qAbs(shown[0].x - width / 2) < 0.5);
        QVERIFY(qAbs(shown[0].y - height / 2) < 0.5);
        QCOMPARE(shown[0].distance, 6.0);
        QCOMPARE(shown[0].opacity, 1.0);
    }

    void labelsAreNoWiderThanTheGapToTheirNeighbour()
    {
        // Two labels 2 m apart; each may be drawn 1.8 m wide, so they do not touch.
        for (const double depth : {-3.5, -6.5, -11.0}) {
            const QList<QcWorldLabel> labels{label({-0.5, 65.6, depth}, u"left"_s), label({1.5, 65.6, depth}, u"right"_s)};
            const QList<QcScreenLabel> shown = QcLabelProjector::visible(labelsAt(labels), northView(), nullptr);
            QCOMPARE(shown.size(), 2);
            const double gap = std::abs(shown[0].x - shown[1].x);
            for (const QcScreenLabel &screen : shown) {
                const double drawn = screen.maxWidth * screen.scale;
                QVERIFY2(drawn < gap && drawn > 0.85 * gap, qPrintable(u"%1 of %2"_s.arg(drawn).arg(gap)));
            }
        }
    }

    void sidesAndHeightLandOnTheRightSide()
    {
        // East (+x) is to the right, higher up is nearer the top of the view.
        const QList<QcScreenLabel> shown =
            QcLabelProjector::visible(labelsAt({label({2.5, 67.6, -5.5}, u"up right"_s)}), northView(), nullptr);
        QCOMPARE(shown.size(), 1);
        QVERIFY(shown[0].x > width / 2 + 50);
        QVERIFY(shown[0].y < height / 2 - 50);
    }

    void behindTooFarOrOutsideIsNotShown()
    {
        const QList<QcWorldLabel> labels{label({0.5, 65.6, 6.5}, u"behind"_s), label({0.5, 65.6, -14.5}, u"far"_s),
                                         label({40.5, 65.6, -5.5}, u"aside"_s), label({0.5, 65.6, -30.5}, u"title"_s, true)};
        const QList<QcScreenLabel> shown = QcLabelProjector::visible(labelsAt(labels), northView(), nullptr);
        QCOMPARE(shown.size(), 1);
        // Titles are seen from further away.
        QCOMPARE(shown[0].text, u"title"_s);
        QVERIFY(shown[0].heading);
    }

    void fadesOutTowardsTheEndOfItsReach()
    {
        const QList<QcWorldLabel> labels{label({0.5, 65.6, -3.5}, u"near"_s), label({0.5, 65.6, -11.0}, u"fading"_s)};
        const QList<QcScreenLabel> shown = QcLabelProjector::visible(labelsAt(labels), northView(), nullptr);
        QCOMPARE(shown.size(), 2);
        // Farthest first; the far one is fading and smaller.
        QCOMPARE(shown[0].text, u"fading"_s);
        QVERIFY(shown[0].opacity > 0.0 && shown[0].opacity < 1.0);
        QVERIFY(shown[0].scale < shown[1].scale);
        QCOMPARE(shown[1].opacity, 1.0);
    }

    void wallsHideLabelsButGlassDoesNot()
    {
        QcContentRegistry content = qcMakeTestContent();
        QcBlockDefinition glass;
        glass.name = u"test:glass"_s;
        glass.renderLayer = QcRenderLayer::Translucent;
        glass.opaque = false;
        glass.lightOpacity = 0;
        glass.faceTextures.fill(u"test:glass"_s);
        QVERIFY(content.registerBlock(glass));

        QcTestWorld world(content);
        // Stone in front of the left label, glass in front of the right one: the lines of sight
        // cross z = -3 at x = -1.0 and x = 2.5.
        world.set({-2, 65, -3}, *content.blockId(u"test:stone"_s));
        world.set({-1, 65, -3}, *content.blockId(u"test:stone"_s));
        world.set({2, 65, -3}, *content.blockId(u"test:glass"_s));
        const QList<QcWorldLabel> labels{label({-2.5, 65.6, -6.5}, u"behind stone"_s), label({4.5, 65.6, -6.5}, u"behind glass"_s)};

        const QList<QcScreenLabel> shown = QcLabelProjector::visible(labelsAt(labels), northView(), &world);
        QCOMPARE(shown.size(), 1);
        QCOMPARE(shown[0].text, u"behind glass"_s);
        QVERIFY(QcLabelProjector::isHidden(world, {0.5, 65.6, 0.5}, {-2.5, 65.6, -6.5}));
    }

    void onlyOpaqueBlocksHideBehindGlass()
    {
        QcContentRegistry content = qcMakeTestContent();
        QcBlockDefinition glass;
        glass.name = u"test:glass"_s;
        glass.renderLayer = QcRenderLayer::Translucent;
        glass.opaque = false;
        glass.lightOpacity = 0;
        glass.faceTextures.fill(u"test:glass"_s);
        QVERIFY(content.registerBlock(glass));

        // Glass first, then stone on the same line of sight: the stone still hides the label.
        QcTestWorld world(content);
        world.set({0, 65, -2}, *content.blockId(u"test:glass"_s));
        world.set({0, 65, -4}, *content.blockId(u"test:stone"_s));
        const QcVec3 eye{0.5, 65.6, 0.5};
        QVERIFY(QcLabelProjector::isHidden(world, eye, {0.5, 65.6, -6.5}));
        // In front of the stone, behind the glass, it shows.
        QVERIFY(!QcLabelProjector::isHidden(world, eye, {0.5, 65.6, -3.2}));
        // Unloaded cells hide nothing.
        world.loadedRadius = 0;
        QVERIFY(!QcLabelProjector::isHidden(world, eye, {0.5, 65.6, -6.5}));
    }

    void sightRaysAreLimitedPerFrame()
    {
        // A stone wall at z = -3 with a hole straight ahead; many labels right behind the wall, one
        // farther back seen through the hole.
        QcContentRegistry content = qcMakeTestContent();
        QcTestWorld world(content);
        world.fillBox({-12, 60, -3}, {12, 72, -3}, *content.blockId(u"test:stone"_s));
        world.set({0, 65, -3}, qcAirBlockId);
        QList<QcWorldLabel> labels{label({0.5, 65.6, -10.5}, u"through the hole"_s)};
        const QList<QcScreenLabel> alone = QcLabelProjector::visible(labelsAt(labels), northView(), &world);
        QCOMPARE(alone.size(), 1);

        // Nearer labels hidden by the wall (rows above and below the hole) use up the budget first:
        // the far one is not looked at this frame.
        const std::array<double, 4> rows{63.0, 64.0, 67.0, 68.0};
        for (int i = 0; i < QcLabelProjector::maxSightRays; ++i) {
            labels.append(label({-5.5 + (i % 12), rows[static_cast<std::size_t>(i / 12)], -4.5}, QString::number(i)));
        }
        QVERIFY(QcLabelProjector::visible(labelsAt(labels), northView(), &world).isEmpty());
        // Without a world nothing is hidden and no rays are needed: the nearest maxShown show.
        QCOMPARE(QcLabelProjector::visible(labelsAt(labels), northView(), nullptr).size(), QcLabelProjector::maxShown);
    }

    void showsOnlyTheNearestFew()
    {
        QList<QcWorldLabel> labels;
        for (int i = 0; i < 200; ++i) {
            const int row = i / 20;
            labels.append(label({-6.0 + (i % 20) * 0.6, 65.0 + row * 0.3, -4.5 - (i % 7)}, QString::number(i)));
        }
        const QList<QcScreenLabel> shown = QcLabelProjector::visible(labelsAt(labels), northView(), nullptr);
        QCOMPARE(shown.size(), QcLabelProjector::maxShown);
        for (qsizetype i = 1; i < shown.size(); ++i) {
            QVERIFY(shown[i - 1].distance >= shown[i].distance);
        }
        // Keys tell labels apart.
        QSet<quint64> keys;
        for (const QcScreenLabel &screen : shown) {
            keys.insert(screen.key);
        }
        QCOMPARE(keys.size(), shown.size());
    }

    void emptyViewShowsNothing()
    {
        QcLabelProjector::View view = northView();
        view.width = 0.0;
        QVERIFY(QcLabelProjector::visible(labelsAt({label({0.5, 65.6, -5.5}, u"a"_s)}), view, nullptr).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestLabelProjector)
#include "test_label_projector.moc"
