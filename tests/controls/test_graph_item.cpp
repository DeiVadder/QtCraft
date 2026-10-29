// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/debug/QcGraphItem.h"
#include "common/core/QcDowncast.h"

#include <QSGGeometryNode>
#include <QTest>

#include <memory>

namespace {

// Reaches the scene graph nodes of the graph without a window.
class GraphProbe : public QcGraphItem
{
    Q_OBJECT

public:
    std::unique_ptr<QSGNode> nodes() { return std::unique_ptr<QSGNode>(updatePaintNode(nullptr, nullptr)); }
};

const QSGGeometry *limitGeometry(const QSGNode &root)
{
    return qcDowncast<const QSGGeometryNode>(root.lastChild())->geometry();
}

} // namespace

class TestGraphItem : public QObject
{
    Q_OBJECT

private slots:
    void limitLineInsideTheScale()
    {
        GraphProbe graph;
        graph.setSize({240.0, 40.0});
        graph.setValues({1.0, 2.0, 3.0});
        graph.setMaxValue(100.0);
        graph.setLimit(50.0);
        QVERIFY(graph.limitShown());

        // Halfway up: y is 20 of 40 pixels.
        const std::unique_ptr<QSGNode> root = graph.nodes();
        const QSGGeometry *limit = limitGeometry(*root);
        QCOMPARE(limit->vertexCount(), 2);
        QCOMPARE(limit->vertexDataAsPoint2D()[0].y, 20.0f);
        QCOMPARE(limit->vertexDataAsPoint2D()[1].x, 240.0f);
    }

    void limitAboveTheScaleIsNotDrawn()
    {
        // Ticks of a few milliseconds scale the graph far below the 50 ms budget: a line on the top
        // edge would look like a tick reaching the top.
        GraphProbe graph;
        graph.setSize({240.0, 40.0});
        graph.setValues({1.0, 2.0, 3.0});
        graph.setMaxValue(4.5);
        graph.setLimit(50.0);
        QVERIFY(!graph.limitShown());
        const std::unique_ptr<QSGNode> root = graph.nodes();
        QCOMPARE(limitGeometry(*root)->vertexCount(), 0);
        // The values are drawn as before.
        QCOMPARE(qcDowncast<const QSGGeometryNode>(root->firstChild())->geometry()->vertexCount(), 3);

        graph.setLimit(0.0);
        QVERIFY(!graph.limitShown());
    }
};

QTEST_MAIN(TestGraphItem)
#include "test_graph_item.moc"
