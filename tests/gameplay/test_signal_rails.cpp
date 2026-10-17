#include "common/world/QcMinecartBody.h"
#include "QcRailFixture.h"

#include <cmath>

using namespace Qt::StringLiterals;

// Powered, detector and activator rails (stage 32.2, decisions E-384, E-385): they never bend; a line
// of powered rails passes power on over eight rails; a powered rail speeds carts up, starts a standing
// one away from a wall and an unpowered one brakes; a detector rail gives a signal while a cart is on
// it and lets a comparator read a container cart; a powered activator rail puts the rider out and
// stops a hopper cart, an unpowered one lets it pull again; a cart standing on a powered rail is
// never left alone (a block laid at an end starts it, a rider getting in is put out). Everything in
// all four rotations.
class TestSignalRails : public QcRailFixture
{
    Q_OBJECT

private:
    // A line of rails of block `name` along local z = `z` from x = `first` to `last`.
    void line(int first, int last, int z, const QString &name)
    {
        QList<QcBlockPos> cells;
        for (int x = first; x <= last; ++x) {
            cells.append({x, 0, z});
        }
        lay(cells, name);
    }

    QString turnText() const { return u" (turns %1)"_s.arg(m_turns); }

private slots:
    void signalRailsNeverBend()
    {
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            // A corner of powered rails stays straight; a line of three stays straight; a slope rises.
            lay({{6, 0, 1}, {7, 0, 1}, {7, 0, 2}}, u"powered_rail"_s);
            lay({{1, 0, 5}, {2, 0, 5}, {3, 0, 5}}, u"detector_rail"_s);
            put(8, 0, 5, u"stone"_s);
            lay({{7, 0, 5}, {8, 1, 5}}, u"activator_rail"_s);

            QVERIFY2(ends(7, 0, 1) == u"east,west"_s || ends(7, 0, 1) == u"north,south"_s, qPrintable(ends(7, 0, 1) + turnText()));
            QCOMPARE(ends(6, 0, 1) + turnText(), u"east,west"_s + turnText());
            QCOMPARE(ends(2, 0, 5) + turnText(), u"east,west"_s + turnText());
            QCOMPARE(ends(7, 0, 5) + turnText(), u"east+,west"_s + turnText());
            // The kept axis of the corner is the same in every rotation (the one it was put with).
            QCOMPARE(ends(7, 0, 1) + turnText(), u"north,south"_s + turnText());
        }
    }

    void poweredRailsPassPowerOverEightRails()
    {
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            line(1, 12, 4, u"powered_rail"_s);
            // A step up in the middle: the line goes on over the slope.
            put(13, 0, 4, u"stone"_s);
            lay({{13, 1, 4}}, u"powered_rail"_s);
            put(0, 0, 4, u"lever[attach=floor]"_s);
            ticks(2);
            QVERIFY(!flag(1, 0, 4, u"powered"_s));

            flip(0, 0, 4);
            ticks(2);
            // The rail next to the lever and eight more along the line; the tenth stays off.
            for (int x = 1; x <= 12; ++x) {
                QCOMPARE(flag(x, 0, 4, u"powered"_s), x <= 9);
            }

            // Powered from the middle, the line reaches the rail on top of the slope.
            flip(0, 0, 4);
            put(9, 0, 5, u"lever[attach=floor]"_s);
            flip(9, 0, 5);
            ticks(3);
            for (int x = 1; x <= 12; ++x) {
                QVERIFY2(flag(x, 0, 4, u"powered"_s), qPrintable(u"%1%2 %3"_s.arg(x).arg(turnText(), look(x, 0, 4))));
            }
            QVERIFY2(flag(13, 1, 4, u"powered"_s), qPrintable(turnText()));

            // Off again: no rail keeps the others on.
            flip(9, 0, 5);
            ticks(3);
            for (int x = 1; x <= 12; ++x) {
                QVERIFY2(!flag(x, 0, 4, u"powered"_s), qPrintable(u"%1%2"_s.arg(x).arg(turnText())));
            }
            QVERIFY(!flag(13, 1, 4, u"powered"_s));
        }
    }

    void poweredRailsSpeedUpStartAndBrake()
    {
        QList<QcVec3> first;
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            // Powered: a line with a lever in the middle and a wall at its west end.
            put(1, 0, 2, u"stone"_s);
            line(2, 12, 2, u"powered_rail"_s);
            put(7, 0, 3, u"lever[attach=floor]"_s);
            flip(7, 0, 3);
            ticks(2);
            const quint32 standing = cartAt(2, 0, 2);
            // Unpowered: a cart at 4 m/s stops within a block.
            line(2, 12, 8, u"powered_rail"_s);
            const quint32 braked = cartAt(3, 0, 8, 4.0, 0.0);

            QList<QcVec3> path;
            for (int tick = 0; tick < 12; ++tick) {
                ticks(1);
                path.append(local(cart(standing).motion.position));
            }
            // Started away from the wall and sped up quickly along the powered rails.
            QVERIFY2(path.constLast().x > 5.0, qPrintable(QString::number(path.constLast().x) + turnText()));
            QVERIFY2(cart(standing).motion.velocity.length() > 5.0, qPrintable(QString::number(cart(standing).motion.velocity.length())));
            const QcVec3 stopped = local(cart(braked).motion.position);
            QCOMPARE(cart(braked).motion.velocity.length(), 0.0);
            QVERIFY2(stopped.x < 4.5, qPrintable(QString::number(stopped.x) + turnText()));
            if (m_turns == 0) {
                first = path;
                continue;
            }
            for (qsizetype i = 0; i < path.size(); ++i) {
                QVERIFY2((path[i] - first[i]).length() < 1e-9, qPrintable(u"tick %1%2"_s.arg(i).arg(turnText())));
            }
        }
    }

    void detectorRailSignalsWhileACartIsOnIt()
    {
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            line(1, 3, 4, u"rail"_s);
            lay({{4, 0, 4}}, u"detector_rail"_s);
            line(5, 12, 4, u"rail"_s);
            put(4, 0, 5, u"signal_lamp"_s);
            ticks(2);
            QVERIFY(!flag(4, 0, 4, u"powered"_s));
            QVERIFY(isBlock(4, 0, 5, "signal_lamp"));

            const quint32 id = cartAt(2, 0, 4, 4.0, 0.0);
            bool lit = false;
            for (int tick = 0; tick < 40 && !lit; ++tick) {
                ticks(1);
                lit = isBlock(4, 0, 5, "signal_lamp_lit");
            }
            QVERIFY2(lit, qPrintable(turnText()));
            QVERIFY(flag(4, 0, 4, u"powered"_s));

            // Gone from the rail, the signal ends (the plate looks again after 20 ticks, the lamp
            // goes dark 4 ticks later).
            ticks(60);
            QVERIFY(local(cart(id).motion.position).x > 5.0);
            QVERIFY2(!flag(4, 0, 4, u"powered"_s), qPrintable(turnText()));
            QVERIFY(isBlock(4, 0, 5, "signal_lamp"));
        }
    }

    void comparatorReadsTheCartOnADetectorRail()
    {
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            line(2, 6, 4, u"rail"_s);
            lay({{4, 0, 4}}, u"detector_rail"_s);
            line(2, 6, 9, u"rail"_s);
            lay({{4, 0, 9}}, u"detector_rail"_s);
            // Comparators south of the detectors read them from behind.
            put(4, 0, 5, u"signal_comparator[facing=south]"_s);
            put(4, 0, 10, u"signal_comparator[facing=south]"_s);

            // A chest cart with nine full stacks: 1 + 14 * 9 / 27 = 5.
            const quint32 chest = cartOf(u"qtcraft:chest_minecart"_s, 4, 0, 4);
            for (int slot = 0; slot < 9; ++slot) {
                m_server->carts().find(chest)->stacks[slot] = {item("qtcraft:cobblestone"), 64, 0};
            }
            // A plain cart: the detector's own signal.
            cartAt(4, 0, 9);
            ticks(6);
            QVERIFY(flag(4, 0, 4, u"powered"_s));
            QCOMPARE(power(4, 0, 5), 5);
            QCOMPARE(power(4, 0, 10), 15);

            // The cart is emptied: the comparator follows once it hears of it.
            for (QcItemStack &stack : m_server->carts().find(chest)->stacks) {
                stack = {};
            }
            m_server->noteContents(at(4, 0, 4));
            ticks(4);
            QCOMPARE(power(4, 0, 5), 0);

            // A chest cart joins the plain cart that already presses the other rail: the comparator
            // reads it although the rail stayed pressed, and the rail's own signal again once it is gone.
            const quint32 joining = cartOf(u"qtcraft:chest_minecart"_s, 4, 0, 9);
            for (int slot = 0; slot < 9; ++slot) {
                m_server->carts().find(joining)->stacks[slot] = {item("qtcraft:cobblestone"), 64, 0};
            }
            ticks(4);
            QVERIFY(flag(4, 0, 9, u"powered"_s));
            QCOMPARE(power(4, 0, 10), 5);
            m_server->carts().remove(joining);
            ticks(4);
            QCOMPARE(power(4, 0, 10), 15);
        }
    }

    void aStandingCartOnAPoweredRailStartsWhenABlockIsLaidAtAnEnd()
    {
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            line(2, 8, 2, u"powered_rail"_s);
            put(5, 0, 3, u"lever[attach=floor]"_s);
            flip(5, 0, 3);
            ticks(2);
            QVERIFY(flag(2, 0, 2, u"powered"_s));
            // Nothing at either end: it stands.
            const quint32 id = cartAt(2, 0, 2);
            ticks(10);
            QCOMPARE(local(cart(id).motion.position).x, 2.5);

            // A block laid beyond its west end starts it eastwards.
            put(1, 0, 2, u"stone"_s);
            ticks(12);
            QVERIFY2(local(cart(id).motion.position).x > 3.5, qPrintable(QString::number(local(cart(id).motion.position).x) + turnText()));
        }
    }

    void aRiderGettingIntoACartOnAPoweredActivatorRailIsPutOut()
    {
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            line(1, 6, 4, u"rail"_s);
            lay({{3, 0, 4}}, u"activator_rail"_s);
            put(3, 0, 5, u"lever[attach=floor]"_s);
            flip(3, 0, 5);
            ticks(2);
            QVERIFY(flag(3, 0, 4, u"powered"_s));
            // A cart standing on the powered activator rail for a while, then a player gets in.
            const quint32 id = cartAt(3, 0, 4);
            ticks(10);
            const QcBlockPos stand = at(3, 0, 3);
            chat(u"/tp %1 %2 %3"_s.arg(stand.x + 0.5).arg(stand.y).arg(stand.z + 0.5));
            idle(2);
            send(QcMsg::UseEntity{id});
            idle(3);
            QCOMPARE(cart(id).rider, 0u);
            QCOMPARE(m_client->vehicle, 0u);
            QCOMPARE(local(cart(id).motion.position).x, 3.5);
        }
    }

    void activatorRailPutsTheRiderOutAndStopsHopperCarts()
    {
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            line(1, 12, 4, u"rail"_s);
            lay({{8, 0, 4}}, u"activator_rail"_s);
            lay({{10, 0, 4}}, u"activator_rail"_s);
            put(8, 0, 5, u"lever[attach=floor]"_s);
            flip(8, 0, 5);
            ticks(2);
            QVERIFY(flag(8, 0, 4, u"powered"_s));
            QVERIFY(!flag(10, 0, 4, u"powered"_s));

            const quint32 ridden = cartAt(3, 0, 4);
            const QcBlockPos stand = at(3, 0, 3);
            chat(u"/tp %1 %2 %3"_s.arg(stand.x + 0.5).arg(stand.y).arg(stand.z + 0.5));
            idle(2);
            send(QcMsg::UseEntity{ridden});
            idle(2);
            QCOMPARE(cart(ridden).rider, m_client->entityId);
            m_server->carts().find(ridden)->motion.velocity = direction(3.0, 0.0);
            for (int tick = 0; tick < 60 && cart(ridden).rider != 0; ++tick) {
                ticks(1);
            }
            idle(1);
            QCOMPARE(cart(ridden).rider, 0u);
            QCOMPARE(m_client->vehicle, 0u);
            // It got out on the powered activator rail.
            const double out = local(cart(ridden).motion.position).x;
            QVERIFY2(out > 7.5 && out < 9.5, qPrintable(QString::number(out) + turnText()));

            // A hopper cart over the powered rail stops pulling, over the unpowered one pulls again.
            m_server->carts().remove(ridden);
            chat(u"/tp %1 %2 %3"_s.arg(stand.x + 0.5).arg(stand.y).arg(stand.z + 0.5));
            idle(2);
            const quint32 hopper = cartOf(u"qtcraft:hopper_minecart"_s, 7, 0, 4);
            m_server->carts().find(hopper)->motion.velocity = direction(1.0, 0.0);
            bool disabled = false;
            for (int tick = 0; tick < 40; ++tick) {
                ticks(1);
                disabled = disabled || !cart(hopper).enabled;
            }
            QVERIFY2(disabled, qPrintable(u"%1 %2 %3"_s.arg(QString::number(local(cart(hopper).motion.position).x), look(8, 0, 4), turnText())));
            QVERIFY(!cart(hopper).enabled);
            m_server->carts().find(hopper)->motion.velocity = direction(0.0, 0.0);
            m_server->carts().find(hopper)->motion.position = point(10.5, 0.0, 4.5);
            ticks(2);
            QVERIFY2(cart(hopper).enabled, qPrintable(turnText()));
            QVERIFY(entityProblems().isEmpty());
        }
    }
};

QTEST_GUILESS_MAIN(TestSignalRails)
#include "test_signal_rails.moc"
