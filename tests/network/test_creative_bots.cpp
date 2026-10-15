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

int countOf(const QJsonObject &inventory, const QString &item)
{
    int total = 0;
    const QJsonArray stacks = inventory[u"slots"_s].toArray();
    for (const auto &slot : stacks) {
        const QJsonObject stack = slot.toObject();
        total += stack.value(u"item"_s) == item ? stack.value(u"count"_s).toInt() : 0;
    }
    return total;
}

} // namespace

// The creative mode over TCP (decisions E-282 to E-285): a dedicated survival server in its own
// process, two bot clients in theirs. The server refuses catalogue items and flight in survival,
// lets only the operator change her mode, and in creative mode the operator's bot flies, takes
// items from the catalogue, breaks blocks at once and picks blocks; the other bot sees the broken
// block. Bob, a survival player, falls out of the world (decision E-288). The slots run in order
// on one server.
class TestCreativeBots : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_root;
    QProcess m_server;
    QString m_serverOutput;
    quint16 m_port = 0;
    QcBotProcess m_alice{u"Alice"_s, m_root.filePath(u"bots/Alice"_s)};
    QcBotProcess m_bob{u"Bob"_s, m_root.filePath(u"bots/Bob"_s)};

    void readServer() { m_serverOutput += QString::fromUtf8(m_server.readAll()); }

    bool console(const QString &line, const QString &expected)
    {
        m_server.write(line.toUtf8() + '\n');
        const qsizetype from = m_serverOutput.size();
        return QTest::qWaitFor([&] {
            readServer();
            return m_serverOutput.indexOf(expected, from) >= 0;
        }, timeoutMs);
    }

    void joinAndWait(QcBotProcess &joining) const
    {
        QVERIFY(joining.start(headless, m_port));
        const QJsonObject ready = joining.request(command(u"waitUntilReady"_s), timeoutMs);
        QVERIFY2(ready[u"ok"_s].toBool(), qPrintable(describe(ready) + joining.errorOutput()));
    }

    QJsonObject status(QcBotProcess &bot) { return bot.request(command(u"status"_s)); }

private slots:
    void initTestCase()
    {
        QVERIFY(m_root.isValid());
        m_server.setProgram(headless);
        m_server.setArguments({u"--server"_s, u"--console"_s, u"--data-dir"_s, m_root.filePath(u"server"_s), u"--bind"_s, u"127.0.0.1"_s,
                               u"--port"_s, u"0"_s, u"--no-mods"_s, u"--generator"_s, u"flat"_s, u"--seed"_s, u"7"_s,
                               u"--view-distance"_s, u"2"_s, u"--time-limit"_s, u"600"_s});
        m_server.setProcessChannelMode(QProcess::MergedChannels);
        m_server.start();
        QVERIFY(m_server.waitForStarted(timeoutMs));

        static const QRegularExpression listening(u"Listening on port (\\d+)"_s);
        QVERIFY2(QTest::qWaitFor([&] {
            readServer();
            m_port = quint16(listening.match(m_serverOutput).captured(1).toUInt());
            return m_port != 0 || m_server.state() == QProcess::NotRunning;
        }, timeoutMs), qPrintable(m_serverOutput));
        QVERIFY(console(u"gamerule mobSpawning false"_s, u"mobSpawning"_s));

        joinAndWait(m_alice);
        joinAndWait(m_bob);
        QVERIFY(console(u"op Alice"_s, u"Alice is an operator"_s));
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

    void survivalRefusesTheCatalogueAndFlight()
    {
        QCOMPARE(status(m_alice).value(u"gameMode"_s).toString(), u"survival"_s);
        const QJsonObject inventory = m_alice.request(command(u"creativeItem"_s, {{u"item"_s, u"diamond"_s}}));
        QCOMPARE(countOf(inventory, u"qtcraft:diamond"_s), 0);
        QVERIFY(!m_alice.request(command(u"fly"_s)).value(u"ok"_s).toBool());
    }

    void onlyOperatorsChangeTheirMode()
    {
        const QJsonObject refused = m_bob.request(command(u"chat"_s, {{u"text"_s, u"/gamemode creative"_s}}));
        QVERIFY2(refused[u"messages"_s].toArray().contains(u"Only operators can use /gamemode"_s), qPrintable(describe(refused)));
        QCOMPARE(status(m_bob).value(u"gameMode"_s).toString(), u"survival"_s);

        const QJsonObject changed = m_alice.request(command(u"gamemode"_s, {{u"mode"_s, u"creative"_s}}));
        QVERIFY2(changed[u"ok"_s].toBool(), qPrintable(describe(changed)));
        QCOMPARE(changed[u"gameMode"_s].toString(), u"creative"_s);
    }

    void creativeFliesAndLands()
    {
        const double ground = status(m_alice).value(u"position"_s).toArray()[1].toDouble();
        QVERIFY(m_alice.request(command(u"fly"_s)).value(u"ok"_s).toBool());
        const QJsonObject up = m_alice.request(command(u"move"_s, {{u"jump"_s, true}, {u"ticks"_s, 20}}));
        QVERIFY2(up[u"flying"_s].toBool() && up[u"position"_s].toArray()[1].toDouble() > ground + 3.0, qPrintable(describe(up)));

        // Bob sees Alice up in the air.
        const QJsonObject seen = m_bob.poll(command(u"entities"_s), [ground](const QJsonObject &answer) {
            const QJsonArray entities = answer[u"entities"_s].toArray();
            return std::ranges::any_of(entities, [ground](const QJsonValue &entity) {
                const QJsonObject object = entity.toObject();
                return object[u"name"_s] == u"Alice"_s && object[u"position"_s].toArray()[1].toDouble() > ground + 3.0;
            });
        }, timeoutMs);
        QVERIFY2(!seen.contains(u"pollTimedOut"_s), qPrintable(describe(seen)));

        const QJsonObject down = m_alice.request(command(u"move"_s, {{u"sneak"_s, true}, {u"ticks"_s, 60}}));
        QVERIFY2(!down[u"flying"_s].toBool() && down[u"onGround"_s].toBool(), qPrintable(describe(down)));
        QCOMPARE(down[u"health"_s].toDouble(), 20.0);
    }

    void catalogueBreakingAndPicking()
    {
        const QJsonObject inventory = m_alice.request(command(u"creativeItem"_s, {{u"item"_s, u"oak_planks"_s}, {u"slot"_s, 4}}));
        QCOMPARE(countOf(inventory, u"qtcraft:oak_planks"_s), 64);

        // The block under Alice's feet breaks without waiting, and Bob's world loses it too.
        const QJsonArray position = status(m_alice).value(u"position"_s).toArray();
        const int x = int(std::floor(position[0].toDouble()));
        const int y = int(std::floor(position[1].toDouble())) - 1;
        const int z = int(std::floor(position[2].toDouble()));
        const QJsonObject below = command(u"block"_s, {{u"x"_s, x}, {u"y"_s, y}, {u"z"_s, z}});
        const QString before = m_alice.request(below).value(u"block"_s).toString();
        QVERIFY(!before.isEmpty() && before != u"qtcraft:air"_s);

        // Picking it gives a full stack of it (nothing of it is in the inventory yet).
        const QJsonObject picked = m_alice.request(command(u"pickBlock"_s, {{u"x"_s, x}, {u"y"_s, y}, {u"z"_s, z}}));
        QVERIFY2(countOf(picked, before) > 0, qPrintable(describe(picked)));

        // Counted in the bot's ticks, so a slow run (sanitizers) does not count against it: by hand
        // the grass or dirt of the flat world takes 18 or 15 ticks.
        const qint64 startTick = status(m_alice).value(u"tick"_s).toInteger();
        const QJsonObject mined = m_alice.request(command(u"mine"_s, {{u"x"_s, x}, {u"y"_s, y}, {u"z"_s, z}}));
        QVERIFY2(mined[u"ok"_s].toBool(), qPrintable(describe(mined)));
        const qint64 ticks = status(m_alice).value(u"tick"_s).toInteger() - startTick;
        QVERIFY2(ticks < 10, qPrintable(QString::number(ticks)));
        const QJsonObject gone = m_bob.poll(below, [](const QJsonObject &answer) { return answer[u"block"_s] == u"qtcraft:air"_s; }, timeoutMs);
        QVERIFY2(!gone.contains(u"pollTimedOut"_s), qPrintable(describe(gone)));
    }

    void survivalInventoryComesBack()
    {
        QVERIFY(m_alice.request(command(u"gamemode"_s, {{u"mode"_s, u"survival"_s}})).value(u"ok"_s).toBool());
        const QJsonObject inventory = m_alice.request(command(u"inventory"_s));
        QCOMPARE(countOf(inventory, u"qtcraft:oak_planks"_s), 0);
        QVERIFY(!m_alice.request(command(u"fly"_s)).value(u"ok"_s).toBool());
    }

    void theVoidKillsOverTcp()
    {
        // Teleported into the void by the console, Bob keeps falling and dies of void damage
        // within 41 ticks (five hits of 4, decision E-288), then respawns at the spawn.
        const QJsonArray position = status(m_bob).value(u"position"_s).toArray();
        const QString destination = u"%1 -70 %2"_s.arg(position.at(0).toDouble()).arg(position.at(2).toDouble());
        QVERIFY(console(u"tp Bob "_s + destination, u"Teleported Bob"_s));

        const QJsonObject fallen = m_bob.poll(command(u"status"_s), [](const QJsonObject &answer) { return answer.value(u"dead"_s).toBool(); },
                                              timeoutMs);
        QVERIFY2(!fallen.contains(u"pollTimedOut"_s), qPrintable(describe(fallen)));
        QVERIFY2(fallen.value(u"position"_s).toArray().at(1).toDouble() < -70.0, qPrintable(describe(fallen)));
        const QJsonArray lines = m_bob.request(command(u"chatLog"_s)).value(u"lines"_s).toArray();
        QVERIFY2(lines.contains(u"Bob fell out of the world"_s), QJsonDocument(lines).toJson(QJsonDocument::Compact).constData());

        const QJsonObject respawned = m_bob.request(command(u"respawn"_s), timeoutMs);
        QVERIFY2(respawned.value(u"ok"_s).toBool(), qPrintable(describe(respawned)));
        // The new position follows the health message with the next player state.
        const QJsonObject back = m_bob.poll(command(u"status"_s), [](const QJsonObject &answer) {
            return answer.value(u"position"_s).toArray().at(1).toDouble() > 0.0;
        }, timeoutMs);
        QVERIFY2(!back.contains(u"pollTimedOut"_s), qPrintable(describe(back)));
    }
};

QTEST_GUILESS_MAIN(TestCreativeBots)
#include "test_creative_bots.moc"
