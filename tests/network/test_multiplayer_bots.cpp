#include "QcBotProcess.h"
#include "server/storage/QcWorldStorage.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 60'000;
const QString headless = QStringLiteral(QTCRAFT_HEADLESS_PATH);

QJsonObject command(const QString &name, QJsonObject arguments = {})
{
    arguments.insert(u"cmd"_s, name);
    return arguments;
}

QJsonObject blockAt(int x, int y, int z)
{
    return command(u"block"_s, {{u"x"_s, x}, {u"y"_s, y}, {u"z"_s, z}});
}

// The other players a bot sees, by name.
QHash<QString, QJsonObject> playersSeen(const QJsonObject &entities)
{
    QHash<QString, QJsonObject> players;
    const QJsonArray list = entities.value(u"entities"_s).toArray();
    for (const auto &entity : list) {
        const QJsonObject object = entity.toObject();
        if (object.value(u"type"_s) == u"player"_s) {
            players.insert(object.value(u"name"_s).toString(), object);
        }
    }
    return players;
}

QStringList playerNames(const QJsonObject &players)
{
    QStringList names;
    const QJsonArray list = players.value(u"players"_s).toArray();
    for (const auto &player : list) {
        names.append(player.toObject().value(u"name"_s).toString());
    }
    names.sort();
    return names;
}

QString describe(const QJsonObject &response)
{
    return QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact));
}

} // namespace

// Several bot clients in their own processes play on a dedicated server in a further process, all
// over TCP on 127.0.0.1 (decision E-270): they see each other with look, armour and animation, chat
// reaches everyone, block, liquid, light and time-of-day changes reach every client, a player who
// leaves disappears and is saved, a rejoin restores position and inventory, and operator commands
// work only for the bot the console made an operator. A refused join and a stopping server end the
// bot processes with their exit codes. The slots run in order on one server.
class TestMultiplayerBots : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_root;
    QProcess m_server;
    QString m_serverOutput;
    quint16 m_port = 0;
    std::vector<std::unique_ptr<QcBotProcess>> m_bots;

    QcBotProcess &bot(qsizetype index) { return *m_bots[std::size_t(index)]; }
    QcBotProcess &alice() { return bot(0); }
    QcBotProcess &bob() { return bot(1); }
    QcBotProcess &carol() { return bot(2); }
    QcBotProcess &dave() { return bot(3); }

    QString botDirectory(const QString &name) const { return m_root.filePath(u"bots/"_s + name); }
    QString worldDirectory() const { return m_root.filePath(u"server/worlds/World"_s); }

    void readServer() { m_serverOutput += QString::fromUtf8(m_server.readAll()); }

    // The end of the server's output, or a sanitizer report (it starts with "WARNING") whole.
    QString serverReport()
    {
        readServer();
        const qsizetype report = m_serverOutput.indexOf(u"WARNING:"_s);
        return report >= 0 ? m_serverOutput.mid(report, 30'000) : m_serverOutput.right(8000);
    }

    // A line on the server console; waits until the reply containing `expected` was printed.
    bool console(const QString &line, const QString &expected)
    {
        m_server.write(line.toUtf8() + '\n');
        const qsizetype from = m_serverOutput.size();
        return QTest::qWaitFor([&] {
            readServer();
            return m_serverOutput.indexOf(expected, from) >= 0;
        }, timeoutMs);
    }

    void startServer()
    {
        m_server.setProgram(headless);
        m_server.setArguments({u"--server"_s, u"--console"_s, u"--data-dir"_s, m_root.filePath(u"server"_s), u"--bind"_s, u"127.0.0.1"_s,
                               u"--port"_s, u"0"_s, u"--no-mods"_s, u"--generator"_s, u"flat"_s, u"--seed"_s, u"7"_s,
                               u"--view-distance"_s, u"2"_s, u"--time-limit"_s, u"900"_s});
        m_server.setProcessChannelMode(QProcess::MergedChannels);
        m_server.start();
        QVERIFY(m_server.waitForStarted(timeoutMs));

        static const QRegularExpression listening(u"Listening on port (\\d+)"_s);
        QVERIFY2(QTest::qWaitFor([&] {
            readServer();
            m_port = quint16(listening.match(m_serverOutput).captured(1).toUInt());
            return m_port != 0 || m_server.state() == QProcess::NotRunning;
        }, timeoutMs), qPrintable(m_serverOutput));
        QVERIFY2(m_port != 0, qPrintable(m_serverOutput));
        // Nothing should walk in and hit the bots; the day keeps running.
        QVERIFY(console(u"gamerule mobSpawning false"_s, u"mobSpawning"_s));
    }

    void joinAndWait(QcBotProcess &joining) const
    {
        QVERIFY(joining.start(headless, m_port));
        const QJsonObject ready = joining.request(command(u"waitUntilReady"_s), timeoutMs);
        QVERIFY2(ready[u"ok"_s].toBool(), qPrintable(describe(ready) + joining.errorOutput()));
    }

    // Every bot but `except` until `done` accepts its answer to `request`.
    template<typename Done>
    void pollOthers(const QcBotProcess *except, const QJsonObject &request, Done done)
    {
        for (const auto &other : m_bots) {
            if (other.get() == except || other->process().state() == QProcess::NotRunning) {
                continue;
            }
            const QJsonObject last = other->poll(request, done, timeoutMs);
            QVERIFY2(!last.contains(u"pollTimedOut"_s), qPrintable(other->name() + u": "_s + describe(last)));
        }
    }

    // The block a bot stands next to, one step in the direction it looks (yaw 0, 90, 180 or 270).
    static std::array<int, 3> groundAhead(const QJsonObject &status)
    {
        const QJsonArray position = status[u"position"_s].toArray();
        const double yaw = qDegreesToRadians(status[u"yaw"_s].toDouble());
        const int x = int(std::floor(position[0].toDouble() + 1.5 * std::sin(yaw)));
        const int z = int(std::floor(position[2].toDouble() - 1.5 * std::cos(yaw)));
        return {x, int(std::floor(position[1].toDouble())) - 1, z};
    }

    // The bot mines the grass one step to the west (by hand, as a player), walks into the hole to
    // pick up the dirt and answers where the block was.
    static std::array<int, 3> digAndCollect(QcBotProcess &digger)
    {
        digger.request(command(u"look"_s, {{u"yaw"_s, 270}, {u"pitch"_s, -40}}));
        const std::array<int, 3> ground = groundAhead(digger.request(command(u"status"_s)));
        const auto [x, y, z] = ground;
        const QJsonObject mined = digger.request(command(u"mine"_s, {{u"x"_s, x}, {u"y"_s, y}, {u"z"_s, z}}));
        const QJsonObject collected = digger.request(command(u"collect"_s, {{u"radius"_s, 4}, {u"maxTicks"_s, 100}}));
        const QJsonObject inventory = digger.request(command(u"inventory"_s));
        [&] {
            QVERIFY2(mined[u"ok"_s].toBool(), qPrintable(describe(mined)));
            QVERIFY2(collected[u"ok"_s].toBool(), qPrintable(describe(collected)));
            QVERIFY2(inventory[u"slots"_s].toArray().size() == 1, qPrintable(describe(inventory)));
        }();
        return ground;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_root.isValid());
        startServer();

        // Four bots, each walking a few metres away from the spawn in its own direction.
        const QStringList names{u"Alice"_s, u"Bob"_s, u"Carol"_s, u"Dave"_s};
        for (qsizetype i = 0; i < names.size(); ++i) {
            m_bots.push_back(std::make_unique<QcBotProcess>(names[i], botDirectory(names[i])));
            joinAndWait(*m_bots.back());
            QVERIFY(bot(i).request(command(u"look"_s, {{u"yaw"_s, 90 * i}, {u"pitch"_s, 0}}))[u"ok"_s].toBool());
            QVERIFY(bot(i).request(command(u"move"_s, {{u"forward"_s, 1}, {u"ticks"_s, 12}}))[u"ok"_s].toBool());
        }
    }

    void cleanupTestCase()
    {
        for (const auto &each : m_bots) {
            if (each->process().state() != QProcess::NotRunning) {
                QCOMPARE(each->quit(), 0);
            }
        }
        if (m_server.state() != QProcess::NotRunning) {
            m_server.write("stop\n");
            QVERIFY(m_server.waitForFinished(timeoutMs));
            QCOMPARE(m_server.exitCode(), 0);
        }
    }

    void everyoneSeesEveryone()
    {
        for (const auto &each : m_bots) {
            const QJsonObject players = each->poll(command(u"players"_s), [](const QJsonObject &answer) {
                return answer[u"players"_s].toArray().size() == 4;
            });
            QCOMPARE(playerNames(players), (QStringList{u"Alice"_s, u"Bob"_s, u"Carol"_s, u"Dave"_s}));

            const QJsonObject entities = each->poll(command(u"entities"_s), [](const QJsonObject &answer) {
                return playersSeen(answer).size() == 3;
            });
            QStringList seen = playersSeen(entities).keys();
            seen.sort();
            QVERIFY2(!seen.contains(each->name()) && seen.size() == 3, qPrintable(describe(entities)));
        }
    }

    void operatorCommandsNeedTheOperatorsKey()
    {
        // Bob is no operator: refused, and the time stays.
        QJsonObject refused = bob().request(command(u"chat"_s, {{u"text"_s, u"/time set 300"_s}}));
        QVERIFY2(refused[u"messages"_s].toArray().contains(u"Only operators can use /time"_s), qPrintable(describe(refused)));
        QVERIFY(bob().request(command(u"status"_s))[u"worldTime"_s].toDouble() < 200.0);

        // The console makes Alice an operator, bound to her identity; everyone's list shows it.
        QVERIFY(console(u"op Alice"_s, u"Alice is an operator"_s));
        pollOthers(nullptr, command(u"players"_s), [](const QJsonObject &answer) {
            const QJsonArray players = answer[u"players"_s].toArray();
            return std::ranges::all_of(players, [](const auto &player) {
                const QJsonObject entry = player.toObject();
                return entry.value(u"operator"_s).toBool() == (entry.value(u"name"_s) == u"Alice"_s);
            });
        });

        const QJsonObject done = alice().request(command(u"chat"_s, {{u"text"_s, u"/time set 300"_s}}));
        QVERIFY2(done[u"ok"_s].toBool(), qPrintable(describe(done)));
        refused = bob().request(command(u"chat"_s, {{u"text"_s, u"/time set 10"_s}}));
        QVERIFY2(refused[u"messages"_s].toArray().contains(u"Only operators can use /time"_s), qPrintable(describe(refused)));
    }

    void timeOfDayIsTheSameEverywhere()
    {
        // Alice set it to 300 s; every client follows and they agree within a second or two.
        pollOthers(nullptr, command(u"status"_s), [](const QJsonObject &answer) { return answer[u"worldTime"_s].toDouble() >= 300.0; });
        QList<double> times;
        for (const auto &each : m_bots) {
            times.append(each->request(command(u"status"_s))[u"worldTime"_s].toDouble());
        }
        const auto [low, high] = std::ranges::minmax(times);
        QVERIFY2(low >= 300.0 && high - low < 2.0, qPrintable(u"%1 .. %2"_s.arg(low).arg(high)));
    }

    void lookArmourAndAnimationReachTheOthers()
    {
        QVERIFY(alice().request(command(u"chat"_s, {{u"text"_s, u"/equip iron"_s}}))[u"ok"_s].toBool());
        QVERIFY(bob().request(command(u"appearance"_s, {{u"shirtColor"_s, u"#c03020"_s}, {u"hair"_s, u""_s}}))[u"ok"_s].toBool());
        pollOthers(&alice(), command(u"entities"_s), [](const QJsonObject &answer) {
            return playersSeen(answer).value(u"Alice"_s)[u"armour"_s][u"head"_s] == u"qtcraft:iron_helmet"_s;
        });
        pollOthers(&bob(), command(u"entities"_s), [](const QJsonObject &answer) {
            const QJsonObject look = playersSeen(answer).value(u"Bob"_s)[u"appearance"_s].toObject();
            return look[u"shirtColor"_s] == u"#c03020"_s && look[u"hair"_s] == u""_s;
        });

        // Carol sneaks back and forth while the others watch her walk crouched.
        const int walking = carol().send(command(u"move"_s, {{u"forward"_s, 1}, {u"sneak"_s, true}, {u"ticks"_s, 60}}));
        pollOthers(&carol(), command(u"entities"_s), [](const QJsonObject &answer) {
            const QJsonObject animation = playersSeen(answer).value(u"Carol"_s)[u"animation"_s].toObject();
            return animation[u"sneaking"_s].toBool() && animation[u"speed"_s].toDouble() > 0.3;
        });
        QVERIFY(carol().response(walking)[u"ok"_s].toBool());
        pollOthers(&carol(), command(u"entities"_s), [](const QJsonObject &answer) {
            const QJsonObject animation = playersSeen(answer).value(u"Carol"_s)[u"animation"_s].toObject();
            return !animation[u"sneaking"_s].toBool() && animation[u"speed"_s].toDouble() < 0.05;
        });
    }

    void chatReachesEveryone()
    {
        QVERIFY(carol().request(command(u"chat"_s, {{u"text"_s, u"hello everyone"_s}}))[u"ok"_s].toBool());
        pollOthers(nullptr, command(u"chatLog"_s), [](const QJsonObject &answer) {
            return answer[u"lines"_s].toArray().contains(u"<Carol> hello everyone"_s);
        });
    }

    void minedAndPlacedBlocksReachEveryone()
    {
        // Dave digs out the grass in front of him by hand, picks up the dirt (it lands in the
        // hole) and puts it on the ground next to the hole.
        const std::array<int, 3> dug = digAndCollect(dave());
        const auto [x, y, z] = dug;
        pollOthers(nullptr, blockAt(x, y, z), [](const QJsonObject &answer) { return answer[u"block"_s] == u"qtcraft:air"_s; });

        const QJsonObject placed = dave().request(command(u"place"_s, {{u"x"_s, x + 1}, {u"y"_s, y}, {u"z"_s, z}, {u"face"_s, u"up"_s}}));
        QVERIFY2(placed[u"ok"_s].toBool(), qPrintable(describe(placed)));
        pollOthers(nullptr, blockAt(x + 1, y + 1, z), [](const QJsonObject &answer) { return answer[u"block"_s] == u"qtcraft:dirt"_s; });
    }

    void liquidsAndLightReachEveryone()
    {
        // A water source flows out to its neighbours on every client; a torch lights its
        // surroundings on every client (each computes block light from the blocks it got).
        QVERIFY(alice().request(command(u"chat"_s, {{u"text"_s, u"/setblock 6 64 6 water"_s}}))[u"ok"_s].toBool());
        pollOthers(nullptr, blockAt(8, 64, 6), [](const QJsonObject &answer) { return answer[u"block"_s].toString().contains(u"water"_s); });

        QVERIFY(alice().request(command(u"chat"_s, {{u"text"_s, u"/setblock -6 64 -6 torch"_s}}))[u"ok"_s].toBool());
        pollOthers(nullptr, blockAt(-6, 64, -5), [](const QJsonObject &answer) { return answer[u"blockLight"_s].toInt() >= 12; });

        QVERIFY(alice().request(command(u"chat"_s, {{u"text"_s, u"/setblock -6 64 -6 air"_s}}))[u"ok"_s].toBool());
        pollOthers(nullptr, blockAt(-6, 64, -5), [](const QJsonObject &answer) { return answer[u"blockLight"_s].toInt() == 0; });
    }

    void aPlayerWhoLeavesDisappearsAndIsSaved()
    {
        // Dave digs up some dirt to have something to keep, turns and leaves.
        digAndCollect(dave());
        QVERIFY(dave().request(command(u"look"_s, {{u"yaw"_s, 123}, {u"pitch"_s, 10}}))[u"ok"_s].toBool());
        const QJsonObject status = dave().request(command(u"wait"_s, {{u"ticks"_s, 10}}));
        const QJsonObject inventory = dave().request(command(u"inventory"_s));
        QVERIFY2(!inventory[u"slots"_s].toArray().isEmpty(), qPrintable(describe(inventory)));
        m_davePosition = status[u"position"_s].toArray();
        m_daveSlots = inventory[u"slots"_s].toArray();

        const QDateTime left = QDateTime::currentDateTime().addSecs(-1);
        QCOMPARE(dave().quit(), 0);
        pollOthers(&dave(), command(u"players"_s), [](const QJsonObject &answer) { return !playerNames(answer).contains(u"Dave"_s); });
        pollOthers(&dave(), command(u"entities"_s), [](const QJsonObject &answer) { return !playersSeen(answer).contains(u"Dave"_s); });

        const QFileInfo saved(QDir(worldDirectory()).filePath(u"players/"_s + QcWorldStorage::playerFileName(u"Dave"_s)));
        QVERIFY(QTest::qWaitFor([&] { return QFileInfo(saved.filePath()).lastModified() >= left; }, timeoutMs));
    }

    void aRejoinRestoresPositionAndInventory()
    {
        QCOMPARE(dave().process().state(), QProcess::NotRunning);
        QVERIFY(!m_davePosition.isEmpty());
        joinAndWait(dave());
        const QJsonObject status = dave().request(command(u"status"_s));
        const QJsonArray position = status[u"position"_s].toArray();
        for (qsizetype axis = 0; axis < 3; ++axis) {
            QVERIFY2(std::abs(position[axis].toDouble() - m_davePosition[axis].toDouble()) < 0.01, qPrintable(describe(status)));
        }
        QCOMPARE(dave().request(command(u"inventory"_s))[u"slots"_s].toArray(), m_daveSlots);
        pollOthers(&dave(), command(u"entities"_s), [](const QJsonObject &answer) { return playersSeen(answer).contains(u"Dave"_s); });
    }

    void aRefusedJoinEndsWithItsReason()
    {
        // Another installation (its own key) cannot take a name somebody already uses here.
        QcBotProcess impostor(u"Alice"_s, botDirectory(u"Impostor"_s));
        QVERIFY(impostor.start(headless, m_port));
        QVERIFY(impostor.process().waitForFinished(timeoutMs));
        QCOMPARE(impostor.process().exitCode(), 3);
        QVERIFY2(impostor.errorOutput().contains(u"belongs to another player"_s), qPrintable(impostor.errorOutput()));

        QcBotProcess nobody(u"Erin"_s, botDirectory(u"Erin"_s));
        QVERIFY(nobody.start(headless, 1));
        QVERIFY(nobody.process().waitForFinished(timeoutMs));
        QCOMPARE(nobody.process().exitCode(), 3);
        QVERIFY2(nobody.errorOutput().contains(u"join failed"_s), qPrintable(nobody.errorOutput()));
    }

    void stoppingTheServerEndsTheBots()
    {
        readServer();
        QVERIFY2(m_server.state() == QProcess::Running, qPrintable(serverReport()));
        m_server.write("stop\n");
        QVERIFY(m_server.waitForFinished(timeoutMs));
        readServer();
        QVERIFY2(m_server.exitStatus() == QProcess::NormalExit && m_server.exitCode() == 0, qPrintable(serverReport()));
        for (const auto &each : m_bots) {
            QVERIFY(each->process().waitForFinished(timeoutMs));
            QCOMPARE(each->process().exitCode(), 4);
            QVERIFY2(each->errorOutput().contains(u"The server stopped"_s), qPrintable(each->errorOutput()));
        }
    }

private:
    QJsonArray m_davePosition;
    QJsonArray m_daveSlots;
};

QTEST_GUILESS_MAIN(TestMultiplayerBots)
#include "test_multiplayer_bots.moc"
