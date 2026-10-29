// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "QcRailFixture.h"

#include <cmath>

using namespace Qt::StringLiterals;

// Rail layouts on a running server (decision E-382): a layout and its mirror image take mirrored
// shapes and carry carts along mirrored paths; rails set one by one through the server's block
// updates take the same shapes in any order, also beside a chunk that comes back; a cart running
// into a chunk that is not loaded waits at the last loaded rail with its speed and rolls on once the
// chunk is back.
class TestRailLayouts : public QcRailFixture
{
    Q_OBJECT

private:
    struct Layout
    {
        QList<QcBlockPos> stones;
        QList<QcBlockPos> rails;
    };

    // Lines, a corner, an S bend, a T, a slope and a line passing one block higher beside the end of
    // another line.
    static Layout mixedLayout()
    {
        Layout layout;
        layout.rails = {{1, 0, 1}, {2, 0, 1}, {3, 0, 1}, {6, 0, 1}, {7, 0, 1}, {7, 0, 2}, {10, 0, 1}, {11, 0, 1}, {11, 0, 2}, {12, 0, 2},
                        {1, 0, 5}, {2, 0, 5}, {3, 0, 5}, {2, 0, 6}, {6, 0, 5}, {7, 0, 5}, {8, 1, 5}, {9, 1, 5}, {4, 0, 9}, {5, 0, 9}, {6, 0, 9}};
        layout.stones = {{8, 0, 5}, {9, 0, 5}};
        for (int x = 3; x <= 9; ++x) {
            layout.stones.append({x, 0, 10});
            layout.rails.append({x, 1, 10});
        }
        return layout;
    }

    // The cell mirrored across the middle of the area (west and east swap).
    static QcBlockPos mirrored(QcBlockPos cell) { return {areaSize - 1 - cell.x, cell.y, cell.z}; }

    static QString mirroredEnds(const QString &ends)
    {
        QStringList names = ends.split(u',');
        for (QString &name : names) {
            name.replace(u"east"_s, u"WEST"_s).replace(u"west"_s, u"east"_s).replace(u"WEST"_s, u"west"_s);
        }
        names.sort();
        return names.join(u',');
    }

    void build(const Layout &layout, bool mirror)
    {
        for (const QcBlockPos &stone : layout.stones) {
            const QcBlockPos cell = mirror ? mirrored(stone) : stone;
            put(cell.x, cell.y, cell.z, u"stone"_s);
        }
        QList<QcBlockPos> rails;
        for (const QcBlockPos &rail : layout.rails) {
            rails.append(mirror ? mirrored(rail) : rail);
        }
        lay(rails);
    }

    // The ends of every rail of the layout, in the layout's order.
    QStringList endsOf(const Layout &layout, bool mirror) const
    {
        QStringList all;
        for (const QcBlockPos &rail : layout.rails) {
            const QcBlockPos cell = mirror ? mirrored(rail) : rail;
            all.append(ends(cell.x, cell.y, cell.z));
        }
        return all;
    }

    // The path of a cart sent from `start` (local) with the local velocity (dx, 0) for `count` ticks.
    QList<QcVec3> pathFrom(QcBlockPos start, double dx, int count)
    {
        const quint32 id = cartAt(start.x, start.y, start.z, dx, 0.0);
        QList<QcVec3> path;
        for (int tick = 0; tick < count; ++tick) {
            ticks(1);
            path.append(local(cart(id).motion.position));
        }
        m_server->carts().remove(id);
        return path;
    }

    static void expectMirrored(const QList<QcVec3> &path, const QList<QcVec3> &mirror, const QString &what)
    {
        QCOMPARE(path.size(), mirror.size());
        for (qsizetype i = 0; i < path.size(); ++i) {
            const QcVec3 back{areaSize - mirror[i].x, mirror[i].y, mirror[i].z};
            QVERIFY2((back - path[i]).length() < 1e-9, qPrintable(u"%1, tick %2"_s.arg(what).arg(i)));
        }
    }

private slots:
    void railShapesAreMirrorImages()
    {
        const Layout layout = mixedLayout();
        for (m_turns = 0; m_turns < 2; ++m_turns) {
            buildArea();
            build(layout, false);
            const QStringList plain = endsOf(layout, false);
            buildArea();
            build(layout, true);
            const QStringList mirror = endsOf(layout, true);
            for (qsizetype i = 0; i < plain.size(); ++i) {
                QVERIFY2(mirroredEnds(mirror[i]) == plain[i], qPrintable(u"rail %1, turns %2: %3 mirrored is %4"_s.arg(i).arg(m_turns).arg(plain[i], mirror[i])));
            }
        }
    }

    void cartPathsAreMirrorImages()
    {
        // A 6 x 6 loop at full speed, and a cart rolling down a slope onto level track.
        Layout loop;
        for (int i = 2; i <= 7; ++i) {
            loop.rails.append({{i, 0, 2}, {i, 0, 7}});
            if (i > 2 && i < 7) {
                loop.rails.append({{2, 0, i}, {7, 0, i}});
            }
        }
        m_turns = 0;
        Layout slope;
        slope.stones = {{1, 0, 11}};
        slope.rails = {{1, 1, 11}};
        for (int x = 2; x <= 12; ++x) {
            slope.rails.append({x, 0, 11});
        }

        buildArea();
        build(loop, false);
        build(slope, false);
        const QList<QcVec3> round = pathFrom({4, 0, 2}, QcMinecartMotion::maxSpeed, 40);
        const QList<QcVec3> down = pathFrom({1, 1, 11}, 1.0, 60);
        buildArea();
        build(loop, true);
        build(slope, true);
        expectMirrored(round, pathFrom(mirrored({4, 0, 2}), -QcMinecartMotion::maxSpeed, 40), u"loop"_s);
        expectMirrored(down, pathFrom(mirrored({1, 1, 11}), -1.0, 60), u"slope"_s);
    }

    void railsTakeTheSameShapesInAnyOrder()
    {
        const Layout layout = mixedLayout();
        m_turns = 0;
        // A local player: his commands are not rate limited.
        QcServerTestFixture::m_origin = QcServer::ClientOrigin::Local;
        buildArea();
        build(layout, false);
        const QStringList expected = endsOf(layout, false);

        // Set one by one as a command does, each followed by the block updates it starts: forwards,
        // backwards and in a scattered order.
        const qsizetype count = layout.rails.size();
        QList<QList<qsizetype>> orders(3);
        for (qsizetype i = 0; i < count; ++i) {
            orders[0].append(i);
            orders[1].append(count - 1 - i);
            orders[2].append((i * 5) % count);
        }
        QCOMPARE(QSet<qsizetype>(orders[2].cbegin(), orders[2].cend()).size(), count);
        for (const QList<qsizetype> &order : std::as_const(orders)) {
            buildArea();
            for (const QcBlockPos &stone : layout.stones) {
                put(stone.x, stone.y, stone.z, u"stone"_s);
            }
            for (const qsizetype index : order) {
                const QcBlockPos cell = at(layout.rails[index].x, layout.rails[index].y, layout.rails[index].z);
                chat(u"/setblock %1 %2 %3 rail"_s.arg(cell.x).arg(cell.y).arg(cell.z));
                ticks(1);
            }
            ticks(5);
            QCOMPARE(endsOf(layout, false), expected);
        }
        QcServerTestFixture::m_origin = QcServer::ClientOrigin::Remote;
    }

    void railsBesideABorderTakeTheirShapeWhenTheChunkBeyondComesBack()
    {
        m_turns = 0;
        buildArea();
        // An area whose local x 12 lies in the next chunk: a line ends at x 10 beside a line one block
        // higher in the border column (x 11) that passes by and does not meet it.
        const QcBlockPos far = m_feet.offset(4 * QcWorld::chunkSize, 12, 0);
        const QcBlockPos origin = far.offset(((4 - far.x) % 16 + 16) % 16, 0, 0);
        const auto layOut = [this] {
            for (int z = 4; z <= 6; ++z) {
                put(11, 0, z, u"stone"_s);
            }
            lay({{11, 1, 4}, {11, 1, 5}, {11, 1, 6}, {9, 0, 5}, {10, 0, 5}});
        };
        clearArea(origin);
        layOut();
        const QStringList expected{ends(10, 0, 5), ends(11, 1, 5)};
        QCOMPARE(expected, (QStringList{u"east,west"_s, u"north,south"_s}));

        // The same with the chunk beyond away while it is laid: whether the higher line meets the
        // lower one depends on its neighbours, some of them beyond the border, so the end of the
        // lower line stays as it was set until the chunk comes back; then it takes its shape.
        clearArea(origin);
        const QcChunkPos beyond = at(12, 0, 5).chunk();
        const std::shared_ptr<QcChunk> taken = m_server->world().takeChunk(beyond);
        QVERIFY(taken);
        put(10, 0, 5, u"rail[facing=north,shape=straight]"_s);
        layOut();
        QCOMPARE(ends(10, 0, 5), u"north,south"_s);
        m_server->world().adoptStoredChunks({{beyond, taken}});
        ticks(3);
        QCOMPARE((QStringList{ends(10, 0, 5), ends(11, 1, 5)}), expected);
    }

    void aCartRunningIntoAChunkThatIsNotLoadedWaitsAtTheLastLoadedRail()
    {
        m_turns = 0;
        buildArea();
        // An area whose local x 12 lies in the next chunk.
        const QcBlockPos far = m_feet.offset(4 * QcWorld::chunkSize, 12, 0);
        clearArea(far.offset(((4 - far.x) % 16 + 16) % 16, 0, 0));
        const int border = 12;
        QVERIFY(at(border, 0, 5).chunk() != at(border - 1, 0, 5).chunk());
        QList<QcBlockPos> line;
        for (int x = 3; x <= 13; ++x) {
            line.append({x, 0, 5});
        }
        lay(line);
        ticks(2);

        const std::shared_ptr<QcChunk> taken = m_server->world().takeChunk(at(border, 0, 5).chunk());
        QVERIFY(taken);
        const quint32 id = cartAt(5, 0, 5, QcMinecartMotion::maxSpeed, 0.0);
        ticks(40);
        // It waits at the end of the loaded track, awake, on its rail and with speed left.
        const QcVec3 waiting = local(cart(id).motion.position);
        QVERIFY2(waiting.x > border - 0.1 && waiting.x < border, qPrintable(QString::number(waiting.x)));
        QVERIFY2(cart(id).motion.velocity.dot(direction(1.0, 0.0)) > 0.5, qPrintable(QString::number(cart(id).motion.velocity.length())));
        QVERIFY(!cart(id).dormant);
        QVERIFY(QcMinecartMotion::segmentAt(cart(id).motion.position, m_server->carts().rails(), m_server->world()));
        ticks(2);
        QCOMPARE(local(cart(id).motion.position).x, waiting.x);
        QVERIFY(entityProblems().isEmpty());

        // The chunk comes back: it rolls on into it.
        m_server->world().adoptStoredChunks({{at(border, 0, 5).chunk(), taken}});
        ticks(10);
        QVERIFY2(local(cart(id).motion.position).x > border + 0.3, qPrintable(QString::number(local(cart(id).motion.position).x)));
        QVERIFY(entityProblems().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestRailLayouts)
#include "test_rail_layouts.moc"
