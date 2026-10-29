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

// Riding a minecart over TCP (stage 32.1, decision E-382): a dedicated server in its own process
// lays a track by console; a bot client in its own process places a cart, gets in through the
// control interface, pushes it along with the forward key and gets out again. The server moves the
// rider; the bot's client shows itself in the cart and never corrects its prediction, also when it
// gets in and out. A second bot watches the cart roll.
class TestMinecartBots : public QObject
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

    bool put(int dx, int dy, int dz, const QString &block)
    {
        return console(u"setblock %1 %2 %3 %4"_s.arg(m_x + dx).arg(m_y + dy).arg(m_z + dz).arg(block), u"Set qtcraft:"_s);
    }

    void joinAndWait(QcBotProcess &joining) const
    {
        QVERIFY(joining.start(headless, m_port));
        const QJsonObject ready = joining.request(command(u"waitUntilReady"_s), timeoutMs);
        QVERIFY2(ready.value(u"ok"_s).toBool(), qPrintable(describe(ready) + joining.errorOutput()));
    }

    // The id of the first minecart `bot` sees; -1 for none.
    static qint64 cartSeenBy(QcBotProcess &bot)
    {
        const QJsonArray entities = bot.request(command(u"entities"_s, {{u"radius"_s, 64}})).value(u"entities"_s).toArray();
        for (const auto &entity : entities) {
            if (entity.toObject().value(u"type"_s) == u"minecart"_s) {
                return entity.toObject().value(u"id"_s).toInteger();
            }
        }
        return -1;
    }

    static QJsonArray cartPosition(QcBotProcess &bot, qint64 id)
    {
        const QJsonArray entities = bot.request(command(u"entities"_s, {{u"radius"_s, 64}})).value(u"entities"_s).toArray();
        for (const auto &entity : entities) {
            if (entity.toObject().value(u"id"_s).toInteger() == id) {
                return entity.toObject().value(u"position"_s).toArray();
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
                               u"--port"_s, u"0"_s, u"--no-mods"_s, u"--generator"_s, u"flat"_s, u"--seed"_s, u"32"_s,
                               u"--view-distance"_s, u"2"_s, u"--time-limit"_s, u"600"_s});
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

    void rideWithoutCorrections()
    {
        // A straight track east of Alice, two blocks to her side.
        for (int dx = 0; dx <= 24; ++dx) {
            QVERIFY(put(dx, 0, 2, u"rail"_s));
        }
        const QJsonObject given = m_alice.request(command(u"chat"_s, {{u"text"_s, u"/give minecart"_s}}));
        QVERIFY2(given.value(u"ok"_s).toBool(), qPrintable(describe(given)));
        QVERIFY(m_alice.request(command(u"wait"_s, {{u"ticks"_s, 5}})).value(u"ok"_s).toBool());
        // Looking at the top of the ground under the rail, the view meets the rail on it first.
        const QJsonObject placed =
            m_alice.request(command(u"use"_s, {{u"x"_s, m_x + 1}, {u"y"_s, m_y - 1}, {u"z"_s, m_z + 2}, {u"face"_s, u"up"_s}}));
        QVERIFY2(placed.value(u"ok"_s).toBool(), qPrintable(describe(placed)));

        qint64 cart = -1;
        QVERIFY(QTest::qWaitFor([&] { return (cart = cartSeenBy(m_alice)) >= 0; }, timeoutMs));
        QVERIFY(QTest::qWaitFor([&] { return cartSeenBy(m_bob) == cart; }, timeoutMs));

        const QJsonObject ride = m_alice.request(command(u"ride"_s, {{u"entity"_s, cart}}));
        QVERIFY2(ride.value(u"ok"_s).toBool() && ride.value(u"riding"_s).toInteger() == cart, qPrintable(describe(ride)));

        // Pushed along by the forward key while looking east, the cart and its rider roll.
        QVERIFY(m_alice.request(command(u"look"_s, {{u"yaw"_s, 90.0}, {u"pitch"_s, 0.0}})).value(u"ok"_s).toBool());
        const QJsonObject before = m_alice.request(command(u"status"_s));
        const QJsonObject pushed = m_alice.request(command(u"move"_s, {{u"forward"_s, 1.0}, {u"ticks"_s, 40}}));
        QVERIFY2(pushed.value(u"ok"_s).toBool(), qPrintable(describe(pushed)));
        const QJsonObject rolling = m_alice.request(command(u"wait"_s, {{u"ticks"_s, 20}}));
        const double travelled = rolling.value(u"position"_s).toArray()[0].toDouble() - before.value(u"position"_s).toArray()[0].toDouble();
        QVERIFY2(travelled > 2.0, qPrintable(describe(rolling)));
        QCOMPARE(rolling.value(u"riding"_s).toInteger(), cart);
        QVERIFY2(rolling.value(u"largestCorrection"_s).toDouble() < 1e-9, qPrintable(describe(rolling)));

        // Bob sees the cart where it rolled to.
        QVERIFY(QTest::qWaitFor([&] { return cartPosition(m_bob, cart).at(0).toDouble() > m_x + 3.0; }, timeoutMs));

        // Out again: no correction either, and walking on afterwards predicts as before.
        const QJsonObject left = m_alice.request(command(u"leave"_s));
        QVERIFY2(left.value(u"ok"_s).toBool() && left.value(u"riding"_s).isNull(), qPrintable(describe(left)));
        const QJsonObject walked = m_alice.request(command(u"move"_s, {{u"forward"_s, 1.0}, {u"ticks"_s, 20}}));
        QVERIFY2(walked.value(u"ok"_s).toBool(), qPrintable(describe(walked)));
        QVERIFY2(walked.value(u"largestCorrection"_s).toDouble() < 1e-9, qPrintable(describe(walked)));
    }
};

QTEST_GUILESS_MAIN(TestMinecartBots)
#include "test_minecart_bots.moc"
