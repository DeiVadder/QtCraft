#include "common/physics/QcPhysics.h"
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

// Items of a kind in the slots of a `machine` answer.
int countIn(const QJsonObject &machine, const QString &item)
{
    int total = 0;
    const QJsonArray cells = machine.value(u"slots"_s).toArray();
    for (const auto &slot : cells) {
        const QJsonObject stack = slot.toObject();
        total += stack.value(u"item"_s).toString() == item ? stack.value(u"count"_s).toInt() : 0;
    }
    return total;
}

} // namespace

// Signal machines over TCP (decisions E-342 to E-346): a dedicated server in its own process builds
// by console; bots in their own processes fill a hopper sorter, a dispenser and a hopper through
// their screens. The sorter sends wheat to its chest and the rest on, an observer clock ticks in
// both bots' worlds, a dispenser pours water both see, and a piston moves a hopper with its
// contents while a bot looks into it: the screen closes, the items moved along.
class TestMachineBots : public QObject
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

    QJsonObject blockAt(int dx, int dy, int dz) const
    {
        return command(u"block"_s, {{u"x"_s, m_x + dx}, {u"y"_s, m_y + dy}, {u"z"_s, m_z + dz}});
    }

    // Whether a `block` answer names `value`, or has `property` at `value` when one is given.
    static bool shows(const QJsonObject &block, const QString &value, const QString &property)
    {
        if (property.isEmpty()) {
            return block.value(u"block"_s) == value;
        }
        return block.value(u"state"_s).toObject().value(property).toString() == value;
    }

    // Waits until `bot` sees the block at the offset show `value` (see shows()).
    bool sees(QcBotProcess &bot, QcBlockPos offset, const QString &value, const QString &property = {}) const
    {
        const QJsonObject answer = bot.poll(blockAt(offset.x, offset.y, offset.z), [&](const QJsonObject &block) { return shows(block, value, property); }, timeoutMs);
        if (answer.contains(u"pollTimedOut"_s)) {
            qWarning("%s", qPrintable(describe(answer)));
            return false;
        }
        return true;
    }

    // The bot looks at the base of the lever and uses it.
    void useLever(QcBotProcess &bot, int dx, int dy, int dz) const
    {
        const QJsonArray position = bot.request(command(u"status"_s)).value(u"position"_s).toArray();
        const double toX = m_x + dx + 0.5 - position[0].toDouble();
        const double toY = m_y + dy + 0.1 - (position[1].toDouble() + QcPhysics::playerEyeHeight);
        const double toZ = m_z + dz + 0.5 - position[2].toDouble();
        const double yaw = std::atan2(toX, -toZ) * 180.0 / M_PI;
        const double pitch = std::atan2(toY, std::hypot(toX, toZ)) * 180.0 / M_PI;
        const QJsonObject looked = bot.request(command(u"look"_s, {{u"yaw"_s, yaw}, {u"pitch"_s, pitch}}));
        QVERIFY(looked.value(u"ok"_s).toBool());
        const QJsonObject used = bot.request(command(u"use"_s));
        const QString target = used.value(u"target"_s).toObject().value(u"block"_s).toString();
        QVERIFY2(used.value(u"ok"_s).toBool() && target == u"qtcraft:lever"_s, qPrintable(describe(used)));
    }

    // Puts Alice at the offset (feet).
    void moveAlice(int dx, int dy, int dz)
    {
        const QJsonObject answer = m_alice.request(command(u"chat"_s, {{u"text"_s, u"/tp %1 %2 %3"_s.arg(m_x + dx + 0.5).arg(m_y + dy).arg(m_z + dz + 0.5)}}));
        QVERIFY2(answer.value(u"ok"_s).toBool(), qPrintable(describe(answer)));
        const QJsonObject arrived = m_alice.poll(command(u"status"_s), [&](const QJsonObject &status) {
            const QJsonArray position = status.value(u"position"_s).toArray();
            return std::abs(position[0].toDouble() - (m_x + dx + 0.5)) < 0.1 && std::abs(position[2].toDouble() - (m_z + dz + 0.5)) < 0.1;
        }, timeoutMs);
        QVERIFY(!arrived.contains(u"pollTimedOut"_s));
    }

    QJsonObject open(int dx, int dy, int dz)
    {
        return m_alice.request(command(u"openMachine"_s, {{u"x"_s, m_x + dx}, {u"y"_s, m_y + dy}, {u"z"_s, m_z + dz}}));
    }

    // The inventory slot holding the largest stack of `item`, or -1.
    int slotOf(const QString &item)
    {
        const QJsonArray cells = m_alice.request(command(u"inventory"_s)).value(u"slots"_s).toArray();
        int best = -1;
        int most = 0;
        for (const auto &slot : cells) {
            const QJsonObject stack = slot.toObject();
            if (stack.value(u"item"_s).toString() == item && stack.value(u"count"_s).toInt() > most) {
                most = stack.value(u"count"_s).toInt();
                best = stack.value(u"slot"_s).toInt();
            }
        }
        return best;
    }

    // Moves `count` of `item` from Alice's inventory into slot `slot` of the open container.
    void store(const QString &item, int count, int slot)
    {
        const int from = slotOf(item);
        QVERIFY2(from >= 0, qPrintable(item));
        const QJsonObject moved = m_alice.request(command(u"moveItem"_s, {{u"from"_s, from}, {u"to"_s, 45 + slot}, {u"count"_s, count}}));
        QVERIFY2(moved.value(u"ok"_s).toBool(), qPrintable(describe(moved)));
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
                               u"--port"_s, u"0"_s, u"--no-mods"_s, u"--generator"_s, u"flat"_s, u"--seed"_s, u"292"_s,
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
        const QJsonArray position = m_alice.request(command(u"status"_s)).value(u"position"_s).toArray();
        m_x = int(std::floor(position[0].toDouble()));
        m_y = int(std::floor(position[1].toDouble()));
        m_z = int(std::floor(position[2].toDouble()));
        for (const QString &give : {u"/give wheat 64"_s, u"/give string 64"_s, u"/give string 64"_s, u"/give dirt 16"_s, u"/give water_bucket 1"_s,
                                    u"/give diamond 5"_s}) {
            const QJsonObject answer = m_alice.request(command(u"chat"_s, {{u"text"_s, give}}));
            QVERIFY2(answer.value(u"ok"_s).toBool(), qPrintable(describe(answer)));
        }
        QVERIFY(m_alice.poll(command(u"inventory"_s), [this](const QJsonObject &) { return slotOf(u"qtcraft:diamond"_s) >= 0; }, timeoutMs)
                    .value(u"pollTimedOut"_s)
                    .isUndefined());
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

    void sorterSendsWheatToItsChest()
    {
        // One sorter unit (the showcase's) under a line of three hoppers ending in an overflow chest.
        QVERIFY(put(3, 0, 3, u"chest[facing=south]"_s));
        QVERIFY(put(3, 1, 3, u"signal_hopper[facing=down,enabled=false]"_s));
        QVERIFY(put(3, 2, 3, u"signal_hopper[facing=south]"_s));
        QVERIFY(put(4, 1, 3, u"stone"_s));
        QVERIFY(put(4, 2, 3, u"signal_comparator[facing=east]"_s));
        for (const auto &[dx, dz] : {std::pair{5, 3}, std::pair{6, 3}, std::pair{6, 2}}) {
            QVERIFY(put(dx, 1, dz, u"glass"_s));
            QVERIFY(put(dx, 2, dz, u"signal_dust"_s));
        }
        QVERIFY(put(5, 2, 2, u"stone"_s));
        for (const auto &[dx, dz] : {std::pair{6, 1}, std::pair{5, 1}, std::pair{5, 2}}) {
            QVERIFY(put(dx, 0, dz, u"stone"_s));
            QVERIFY(put(dx, 1, dz, u"signal_dust"_s));
        }
        for (const int dz : {2, 1}) {
            QVERIFY(put(3, 0, dz, u"stone"_s));
        }
        QVERIFY(put(4, 0, 2, u"stone"_s));
        QVERIFY(put(4, 1, 2, u"signal_repeater[facing=west,delay=1]"_s));
        QVERIFY(put(3, 1, 2, u"signal_comparator[facing=south,mode=subtract,powered=true,power=15]"_s));
        QVERIFY(put(3, 1, 1, u"lever[attach=floor,powered=true]"_s));
        QVERIFY(put(1, 0, 3, u"stone"_s));
        QVERIFY(put(1, 1, 3, u"stone"_s));
        QVERIFY(put(1, 2, 3, u"stone"_s));
        for (int x = 2; x <= 4; ++x) {
            QVERIFY(put(x, 3, 3, u"signal_hopper[facing=east]"_s));
        }
        QVERIFY(put(5, 3, 3, u"chest[facing=south]"_s));
        QVERIFY(sees(m_bob, {3, 1, 2}, u"qtcraft:signal_comparator"_s));

        // The filter: 18 wheat and four stacks of 24 string.
        const QJsonObject filter = open(3, 2, 3);
        QVERIFY2(filter.value(u"ok"_s).toBool(), qPrintable(describe(filter)));
        store(u"qtcraft:wheat"_s, 18, 0);
        for (int slot = 1; slot <= 4; ++slot) {
            store(u"qtcraft:string"_s, 24, slot);
        }
        // The first line hopper gets wheat and dirt in turn.
        QVERIFY(open(2, 3, 3).value(u"ok"_s).toBool());
        store(u"qtcraft:wheat"_s, 1, 0);
        store(u"qtcraft:dirt"_s, 1, 1);
        store(u"qtcraft:wheat"_s, 1, 2);
        store(u"qtcraft:dirt"_s, 1, 3);
        store(u"qtcraft:wheat"_s, 1, 4);
        m_alice.request(command(u"closeMachine"_s));

        // Two of the three wheat reach the chest (the last waits in the locked hopper), the dirt
        // passes on into the overflow; Alice looks from a pillar.
        QVERIFY(put(7, 2, 3, u"stone"_s));
        moveAlice(7, 3, 3);
        QVERIFY(open(5, 3, 3).value(u"ok"_s).toBool());
        const QJsonObject overflow = m_alice.poll(command(u"machine"_s), [](const QJsonObject &machine) {
            return countIn(machine, u"qtcraft:dirt"_s) == 2;
        }, timeoutMs);
        QVERIFY2(!overflow.contains(u"pollTimedOut"_s), qPrintable(describe(overflow)));
        QCOMPARE(countIn(overflow, u"qtcraft:wheat"_s), 0);
        m_alice.request(command(u"closeMachine"_s));
        moveAlice(3, 0, 5);
        QVERIFY(open(3, 0, 3).value(u"ok"_s).toBool());
        const QJsonObject sorted = m_alice.poll(command(u"machine"_s), [](const QJsonObject &machine) {
            return countIn(machine, u"qtcraft:wheat"_s) >= 2;
        }, timeoutMs);
        QVERIFY2(!sorted.contains(u"pollTimedOut"_s), qPrintable(describe(sorted)));
        m_alice.request(command(u"closeMachine"_s));
        moveAlice(0, 0, 0);
    }

    void observerClockTicksForBoth()
    {
        QVERIFY(put(-4, 0, 2, u"signal_observer[facing=east,powered=true]"_s));
        QVERIFY(put(-3, 0, 2, u"signal_observer[facing=west]"_s));
        for (QcBotProcess *bot : {&m_alice, &m_bob}) {
            QVERIFY(sees(*bot, {-3, 0, 2}, u"true"_s, u"powered"_s));
            QVERIFY(sees(*bot, {-3, 0, 2}, u"false"_s, u"powered"_s));
            QVERIFY(sees(*bot, {-3, 0, 2}, u"true"_s, u"powered"_s));
        }
    }

    void dispenserPoursWater()
    {
        QVERIFY(put(-2, 0, -3, u"signal_dispenser[facing=west]"_s));
        QVERIFY(put(-2, 0, -2, u"lever[attach=floor]"_s));
        QVERIFY(put(-4, 0, -3, u"glass"_s));
        QVERIFY(put(-3, 0, -4, u"glass"_s));
        QVERIFY(put(-3, 0, -2, u"glass"_s));
        QVERIFY(open(-2, 0, -3).value(u"ok"_s).toBool());
        store(u"qtcraft:water_bucket"_s, 1, 4);
        m_alice.request(command(u"closeMachine"_s));

        useLever(m_bob, -2, 0, -2);
        QVERIFY(sees(m_bob, {-3, 0, -3}, u"qtcraft:water"_s));
        QVERIFY(sees(m_alice, {-3, 0, -3}, u"qtcraft:water"_s));
        QVERIFY(sees(m_alice, {-2, 0, -3}, u"true"_s, u"triggered"_s));
    }

    void pistonMovesAnOpenHopper()
    {
        QVERIFY(put(2, 0, -3, u"lever[attach=floor]"_s));
        QVERIFY(put(3, 0, -3, u"signal_piston[facing=east]"_s));
        QVERIFY(put(4, 0, -3, u"signal_hopper[facing=down]"_s));
        moveAlice(4, 0, -1);
        QVERIFY(open(4, 0, -3).value(u"ok"_s).toBool());
        store(u"qtcraft:diamond"_s, 5, 2);

        useLever(m_bob, 2, 0, -3);
        QVERIFY(sees(m_bob, {5, 0, -3}, u"qtcraft:signal_hopper"_s));
        const QJsonObject closed = m_alice.poll(command(u"machine"_s), [](const QJsonObject &machine) {
            return !machine.value(u"open"_s).toBool();
        }, timeoutMs);
        QVERIFY2(!closed.contains(u"pollTimedOut"_s), qPrintable(describe(closed)));
        const QJsonObject moved = open(5, 0, -3);
        QVERIFY2(moved.value(u"ok"_s).toBool(), qPrintable(describe(moved)));
        QCOMPARE(countIn(m_alice.request(command(u"machine"_s)), u"qtcraft:diamond"_s), 5);
        m_alice.request(command(u"closeMachine"_s));
    }
};

QTEST_GUILESS_MAIN(TestMachineBots)
#include "test_machine_bots.moc"
