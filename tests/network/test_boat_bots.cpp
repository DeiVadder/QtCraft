// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "QcBotProcess.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 60'000;
const QString headless = QStringLiteral(QTCRAFT_HEADLESS_PATH);
// The lake east of Alice: from 2 to lakeEnd blocks east, three blocks to either side, two deep.
constexpr int lakeEnd = 40;

QJsonObject command(const QString &name, QJsonObject arguments = {})
{
    arguments.insert(u"cmd"_s, name);
    return arguments;
}

QString describe(const QJsonObject &response)
{
    return QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact));
}

} // namespace

// Driving a boat over TCP (decision E-920): a dedicated server in its own process digs a lake by
// console; a bot client in its own process places a boat on it, gets in through the control
// interface and paddles across the lake to the far bank, where it gets out onto the shore. The
// client predicts the boat with the shared physics and the server steps it with the same inputs:
// the prediction is never corrected. A second bot watches the boat cross.
class TestBoatBots : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_root;
    QProcess m_server;
    QString m_serverOutput;
    quint16 m_port = 0;
    QcBotProcess m_alice{u"Alice"_s, m_root.filePath(u"bots/Alice"_s)};
    QcBotProcess m_bob{u"Bob"_s, m_root.filePath(u"bots/Bob"_s)};
    int m_x = 0;
    int m_y = 0;
    int m_z = 0;

    void readServer() { m_serverOutput += QString::fromUtf8(m_server.readAll()); }

    bool console(const QString &line, const QString &expected)
    {
        m_server.write(line.toUtf8() + '\n');
        const qsizetype from = m_serverOutput.size();
        const bool answered = QTest::qWaitFor([&] {
            readServer();
            return m_serverOutput.indexOf(expected, from) >= 0;
        }, timeoutMs);
        if (!answered) {
            qWarning("%s", qPrintable(m_serverOutput.mid(from)));
        }
        return answered;
    }

    void joinAndWait(QcBotProcess &joining) const
    {
        QVERIFY(joining.start(headless, m_port));
        const QJsonObject ready = joining.request(command(u"waitUntilReady"_s), timeoutMs);
        QVERIFY2(ready.value(u"ok"_s).toBool(), qPrintable(describe(ready) + joining.errorOutput()));
    }

    // The entity `bot` sees with this id or, for -1, the first boat; empty for none.
    static QJsonObject boatSeenBy(QcBotProcess &bot, qint64 id = -1)
    {
        const QJsonArray entities = bot.request(command(u"entities"_s, {{u"radius"_s, 96}})).value(u"entities"_s).toArray();
        for (const auto &entity : entities) {
            const QJsonObject object = entity.toObject();
            if (object.value(u"type"_s) == u"boat"_s && (id < 0 || object.value(u"id"_s).toInteger() == id)) {
                return object;
            }
        }
        return {};
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_root.isValid());
        m_server.setProgram(headless);
        m_server.setArguments({u"--server"_s, u"--console"_s, u"--data-dir"_s, m_root.filePath(u"server"_s), u"--bind"_s, u"127.0.0.1"_s,
                               u"--port"_s, u"0"_s, u"--no-mods"_s, u"--generator"_s, u"flat"_s, u"--seed"_s, u"79"_s,
                               u"--view-distance"_s, u"4"_s, u"--time-limit"_s, u"600"_s});
        m_server.setProcessChannelMode(QProcess::MergedChannels);
        m_server.start();
        QVERIFY(m_server.waitForStarted(timeoutMs));

        static const QRegularExpression listening(u"Listening on port (\\d+)"_s);
        QVERIFY2(QTest::qWaitFor([&] {
            readServer();
            m_port = static_cast<quint16>(listening.match(m_serverOutput).captured(1).toUInt());
            return m_port != 0 || m_server.state() == QProcess::NotRunning;
        }, timeoutMs), qPrintable(m_serverOutput));
        QVERIFY(console(u"gamerule mobSpawning false"_s, u"mobSpawning"_s));

        joinAndWait(m_alice);
        joinAndWait(m_bob);
        // Bob steps aside to watch; the bots join on the same spot.
        QVERIFY(m_bob.request(command(u"look"_s, {{u"yaw"_s, 270.0}, {u"pitch"_s, 0.0}})).value(u"ok"_s).toBool());
        QVERIFY(m_bob.request(command(u"move"_s, {{u"forward"_s, 1.0}, {u"ticks"_s, 15}})).value(u"ok"_s).toBool());
        QVERIFY(console(u"op Alice"_s, u"Alice"_s));
        const QJsonArray position = m_alice.request(command(u"status"_s)).value(u"position"_s).toArray();
        m_x = static_cast<int>(std::floor(position[0].toDouble()));
        m_y = static_cast<int>(std::floor(position[1].toDouble()));
        m_z = static_cast<int>(std::floor(position[2].toDouble()));
        // The lake, its surface a little below the ground the bots stand on.
        QVERIFY(console(u"fill %1 %2 %3 %4 %5 %6 water"_s.arg(m_x + 2).arg(m_y - 2).arg(m_z - 3).arg(m_x + lakeEnd).arg(m_y - 1).arg(m_z + 3),
                        u"Filled"_s));
    }

    void cleanupTestCase()
    {
        for (QcBotProcess *bot : {&m_alice, &m_bob}) {
            if (bot->process().state() != QProcess::NotRunning) {
                QCOMPARE(bot->quit(), 0);
            }
        }
        if (m_server.state() != QProcess::NotRunning) {
            m_server.write("stop\n");
            QVERIFY(m_server.waitForFinished(timeoutMs));
            QCOMPARE(m_server.exitCode(), 0);
        }
    }

    void driveAcrossTheLakeWithoutCorrections()
    {
        const QJsonObject given = m_alice.request(command(u"chat"_s, {{u"text"_s, u"/give oak_boat"_s}}));
        QVERIFY2(given.value(u"ok"_s).toBool(), qPrintable(describe(given)));
        QVERIFY(m_alice.request(command(u"wait"_s, {{u"ticks"_s, 5}})).value(u"ok"_s).toBool());
        // Used on the water east of Alice, the boat lies on the surface pointing the way she looks.
        const QJsonObject placed = m_alice.request(command(u"use"_s, {{u"x"_s, m_x + 3}, {u"y"_s, m_y - 1}, {u"z"_s, m_z}, {u"face"_s, u"up"_s}}));
        QVERIFY2(placed.value(u"ok"_s).toBool(), qPrintable(describe(placed)));

        qint64 boat = -1;
        QVERIFY(QTest::qWaitFor([&] { return (boat = boatSeenBy(m_alice).value(u"id"_s).toInteger(-1)) >= 0; }, timeoutMs));
        QVERIFY(QTest::qWaitFor([&] { return !boatSeenBy(m_bob, boat).isEmpty(); }, timeoutMs));

        const QJsonObject ride = m_alice.request(command(u"ride"_s, {{u"entity"_s, boat}}));
        QVERIFY2(ride.value(u"ok"_s).toBool() && ride.value(u"riding"_s).toInteger() == boat, qPrintable(describe(ride)));

        // Steer the boat round to the east (it points where Alice looked when she placed it),
        // then paddle until it lies against the far bank.
        const double startX = m_alice.request(command(u"status"_s)).value(u"position"_s).toArray()[0].toDouble();
        for (int turn = 0; turn < 40; ++turn) {
            const double yaw = boatSeenBy(m_alice, boat).value(u"yaw"_s).toDouble();
            const double off = std::remainder(yaw - 90.0, 360.0);
            if (std::abs(off) < 4.0) {
                break;
            }
            const QJsonObject turned = m_alice.request(command(u"move"_s, {{u"strafe"_s, off > 0.0 ? -1.0 : 1.0}, {u"ticks"_s, 1}}));
            QVERIFY2(turned.value(u"ok"_s).toBool(), qPrintable(describe(turned)));
        }
        const QJsonObject paddled = m_alice.request(command(u"move"_s, {{u"forward"_s, 1.0}, {u"ticks"_s, 200}}));
        QVERIFY2(paddled.value(u"ok"_s).toBool(), qPrintable(describe(paddled)));
        const QJsonObject across = m_alice.request(command(u"wait"_s, {{u"ticks"_s, 10}}));
        const double crossed = across.value(u"position"_s).toArray()[0].toDouble() - startX;
        QVERIFY2(crossed > lakeEnd - 10.0, qPrintable(describe(across)));
        QCOMPARE(across.value(u"riding"_s).toInteger(), boat);
        QVERIFY2(across.value(u"largestCorrection"_s).toDouble() < 1e-9, qPrintable(describe(across)));

        // Bob sees the boat at the far bank.
        QVERIFY(QTest::qWaitFor([&] { return boatSeenBy(m_bob, boat).value(u"position"_s).toArray().at(0).toDouble() > m_x + lakeEnd - 5.0; },
                                timeoutMs));

        // Out onto the shore beyond the lake: no correction, and she stands on the ground there.
        const QJsonObject left = m_alice.request(command(u"leave"_s));
        QVERIFY2(left.value(u"ok"_s).toBool() && left.value(u"riding"_s).isNull(), qPrintable(describe(left)));
        const QJsonObject ashore = m_alice.request(command(u"wait"_s, {{u"ticks"_s, 10}}));
        const QJsonArray feet = ashore.value(u"position"_s).toArray();
        QVERIFY2(feet[0].toDouble() > m_x + lakeEnd + 0.9, qPrintable(describe(ashore)));
        QCOMPARE(feet[1].toDouble(), static_cast<double>(m_y));
        QVERIFY2(ashore.value(u"largestCorrection"_s).toDouble() < 1e-9, qPrintable(describe(ashore)));
    }
};

QTEST_GUILESS_MAIN(TestBoatBots)
#include "test_boat_bots.moc"
