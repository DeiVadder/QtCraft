// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/physics/QcPhysics.h"
#include "fixtures/QcBotProcess.h"

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

// Signal Dust over TCP (decisions E-335 to E-339): a dedicated server in its own process builds a
// circuit by console; a bot client in its own process switches a lever, and the lamp behind dust
// and a repeater lights in the bot's world. A second lever closes and opens a piston door; a second
// bot sees the door blocks move. Every state reaches the clients through the block updates.
class TestSignalBots : public QObject
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

    // Sets a block relative to Alice's feet from the server console.
    bool put(int dx, int dy, int dz, const QString &block)
    {
        return console(u"setblock %1 %2 %3 %4"_s.arg(m_x + dx).arg(m_y + dy).arg(m_z + dz).arg(block), u"Set qtcraft:"_s);
    }

    QJsonObject blockAt(int dx, int dy, int dz) const
    {
        return command(u"block"_s, {{u"x"_s, m_x + dx}, {u"y"_s, m_y + dy}, {u"z"_s, m_z + dz}});
    }

    // Waits until `bot` sees the block at the offset become `name`.
    bool sees(QcBotProcess &bot, int dx, int dy, int dz, const QString &name)
    {
        const QJsonObject answer =
            bot.poll(blockAt(dx, dy, dz), [&name](const QJsonObject &block) { return block.value(u"block"_s) == name; }, timeoutMs);
        if (answer.contains(u"pollTimedOut"_s)) {
            qWarning("%s", qPrintable(describe(answer)));
            return false;
        }
        return true;
    }

    // Alice looks at the base of the lever (a small target) and uses it.
    void useLever(int dx, int dy, int dz)
    {
        const QJsonArray position = m_alice.request(command(u"status"_s)).value(u"position"_s).toArray();
        const double toX = m_x + dx + 0.5 - position[0].toDouble();
        const double toY = m_y + dy + 0.1 - (position[1].toDouble() + QcPhysics::playerEyeHeight);
        const double toZ = m_z + dz + 0.5 - position[2].toDouble();
        const double yaw = std::atan2(toX, -toZ) * 180.0 / M_PI;
        const double pitch = std::atan2(toY, std::hypot(toX, toZ)) * 180.0 / M_PI;
        const QJsonObject looked = m_alice.request(command(u"look"_s, {{u"yaw"_s, yaw}, {u"pitch"_s, pitch}}));
        QVERIFY(looked.value(u"ok"_s).toBool());
        const QJsonObject used = m_alice.request(command(u"use"_s));
        const QString target = used.value(u"target"_s).toObject().value(u"block"_s).toString();
        QVERIFY2(used.value(u"ok"_s).toBool() && target == u"qtcraft:lever"_s, qPrintable(describe(used)));
    }

    void joinAndWait(QcBotProcess &joining) const
    {
        QVERIFY(joining.start(headless, m_port));
        const QJsonObject ready = joining.request(command(u"waitUntilReady"_s), timeoutMs);
        QVERIFY2(ready.value(u"ok"_s).toBool(), qPrintable(describe(ready) + joining.errorOutput()));
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_root.isValid());
        m_server.setProgram(headless);
        m_server.setArguments({u"--server"_s, u"--console"_s, u"--data-dir"_s, m_root.filePath(u"server"_s), u"--bind"_s, u"127.0.0.1"_s,
                               u"--port"_s, u"0"_s, u"--no-mods"_s, u"--generator"_s, u"flat"_s, u"--seed"_s, u"29"_s,
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

    void leverLightsALampThroughDustAndARepeater()
    {
        QVERIFY(put(2, 0, 0, u"lever[attach=floor]"_s));
        QVERIFY(put(3, 0, 0, u"signal_dust"_s));
        QVERIFY(put(4, 0, 0, u"signal_dust"_s));
        QVERIFY(put(5, 0, 0, u"signal_repeater[facing=east,delay=2]"_s));
        QVERIFY(put(6, 0, 0, u"signal_dust"_s));
        QVERIFY(put(7, 0, 0, u"signal_lamp"_s));
        QVERIFY(sees(m_alice, 7, 0, 0, u"qtcraft:signal_lamp"_s));

        useLever(2, 0, 0);
        QVERIFY(sees(m_alice, 7, 0, 0, u"qtcraft:signal_lamp_lit"_s));
        QVERIFY(sees(m_bob, 7, 0, 0, u"qtcraft:signal_lamp_lit"_s));
        const QJsonObject dust = m_bob.request(blockAt(3, 0, 0));
        const QString strength = dust.value(u"state"_s).toObject().value(u"power"_s).toString();
        QCOMPARE(strength, u"15"_s);
        const QJsonObject repeater = m_bob.request(blockAt(5, 0, 0));
        const QString powered = repeater.value(u"state"_s).toObject().value(u"powered"_s).toString();
        QCOMPARE(powered, u"true"_s);

        useLever(2, 0, 0);
        QVERIFY(sees(m_bob, 7, 0, 0, u"qtcraft:signal_lamp"_s));
    }

    void pistonDoorClosesAndOpens()
    {
        // Two sticky pistons behind a doorway hold its blocks beside it; the lever on the stone
        // powers both (the upper one directly, the lower one through the stone).
        QVERIFY(put(4, 0, 4, u"sticky_signal_piston[facing=north]"_s));
        QVERIFY(put(4, 1, 4, u"sticky_signal_piston[facing=north]"_s));
        QVERIFY(put(4, 0, 3, u"polished_stone"_s));
        QVERIFY(put(4, 1, 3, u"polished_stone"_s));
        QVERIFY(put(3, 0, 4, u"stone"_s));
        QVERIFY(put(3, 1, 4, u"lever[attach=floor]"_s));
        QVERIFY(sees(m_bob, 4, 0, 2, u"qtcraft:air"_s));

        useLever(3, 1, 4);
        QVERIFY(sees(m_bob, 4, 0, 2, u"qtcraft:polished_stone"_s));
        QVERIFY(sees(m_bob, 4, 1, 2, u"qtcraft:polished_stone"_s));
        QVERIFY(sees(m_bob, 4, 0, 3, u"qtcraft:signal_piston_head"_s));

        // Off: the sticky pistons pull the blocks back, the doorway is open.
        useLever(3, 1, 4);
        QVERIFY(sees(m_bob, 4, 0, 2, u"qtcraft:air"_s));
        QVERIFY(sees(m_bob, 4, 1, 2, u"qtcraft:air"_s));
        QVERIFY(sees(m_alice, 4, 0, 3, u"qtcraft:polished_stone"_s));
        QVERIFY(sees(m_alice, 4, 0, 4, u"qtcraft:sticky_signal_piston"_s));
    }
};

QTEST_GUILESS_MAIN(TestSignalBots)
#include "test_signal_bots.moc"
