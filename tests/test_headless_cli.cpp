#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 30000;

} // namespace

// Drives the qtcraft-headless executable interactively: every response must arrive while stdin is
// still open, before the next request is written.
class TestHeadlessCli : public QObject
{
    Q_OBJECT

private:
    QProcess m_process;

    QJsonObject ask(const QByteArray &line)
    {
        m_process.write(line + '\n');
        if (!m_process.canReadLine() && !QTest::qWaitFor([this] { return m_process.canReadLine(); }, timeoutMs)) {
            qWarning("no response to %s", line.constData());
            return {};
        }
        return QJsonDocument::fromJson(m_process.readLine()).object();
    }

private slots:
    void init()
    {
        m_process.setProgram(QStringLiteral(QTCRAFT_HEADLESS_PATH));
        m_process.setArguments({u"--view-distance"_s, u"2"_s, u"--time-limit"_s, u"90"_s});
        m_process.setProcessChannelMode(QProcess::SeparateChannels);
        m_process.start();
        QVERIFY(m_process.waitForStarted(timeoutMs));
    }

    void cleanup()
    {
        if (m_process.state() != QProcess::NotRunning) {
            m_process.closeWriteChannel();
            QVERIFY(m_process.waitForFinished(timeoutMs));
        }
    }

    void loadsModsFromTheModsDirectory()
    {
        m_process.closeWriteChannel();
        QVERIFY(m_process.waitForFinished(timeoutMs));
        m_process.setArguments({u"--view-distance"_s, u"2"_s, u"--time-limit"_s, u"90"_s, u"--mods-dir"_s,
                                QStringLiteral(QTCRAFT_MODS_SOURCE_DIR)});
        m_process.start();
        QVERIFY(m_process.waitForStarted(timeoutMs));
        QCOMPARE(ask(R"({"cmd":"waitUntilReady"})").value(u"ok"_s).toBool(), true);
        const QJsonArray packs = ask(R"({"cmd":"mods"})").value(u"packs"_s).toArray();
        QCOMPARE(packs.size(), 2);
        QCOMPARE(packs[1].toObject().value(u"id"_s).toString(), u"rubies"_s);
        ask(R"({"cmd":"chat","text":"/give rubies:ruby_sword"})");
        const QJsonArray stacks = ask(R"({"cmd":"inventory"})").value(u"slots"_s).toArray();
        QVERIFY(std::ranges::any_of(stacks, [](const QJsonValue &slot) {
            return slot.toObject().value(u"item"_s).toString() == u"rubies:ruby_sword"_s;
        }));
    }

    void answersEachRequestWhileInputStaysOpen()
    {
        QCOMPARE(ask(R"({"id":1,"cmd":"waitUntilReady"})").value(u"state"_s).toString(), u"Playing"_s);
        QCOMPARE(ask(R"({"id":2,"cmd":"status"})").value(u"id"_s).toInt(), 2);
        const QJsonObject moved = ask(R"({"id":3,"cmd":"move","forward":1,"ticks":5})");
        QCOMPARE(moved.value(u"ok"_s).toBool(), true);
        QCOMPARE(moved.value(u"tick"_s).toInt() >= 5, true);
    }

    void errorsKeepInputOrderAndProcessSurvives()
    {
        QCOMPARE(ask("not json").value(u"ok"_s).toBool(true), false);
        QCOMPARE(ask(R"({"cmd":"look","yaw":"north"})").value(u"ok"_s).toBool(true), false);
        const QJsonObject huge = ask(R"({"cmd":"look","yaw":1e300,"pitch":5})");
        QCOMPARE(huge.value(u"ok"_s).toBool(), true);
        QVERIFY(huge.value(u"yaw"_s).toDouble() >= 0.0 && huge.value(u"yaw"_s).toDouble() < 360.0);
    }

    void endOfInputFinishesQueuedRequestsThenExits()
    {
        m_process.write(R"({"cmd":"waitUntilReady"})" "\n" R"({"cmd":"wait","ticks":10})" "\n");
        m_process.closeWriteChannel();
        QVERIFY(m_process.waitForFinished(timeoutMs));
        QCOMPARE(m_process.exitCode(), 0);
        const QList<QByteArray> lines = m_process.readAllStandardOutput().trimmed().split('\n');
        QCOMPARE(lines.size(), 2);
        QVERIFY(QJsonDocument::fromJson(lines.last()).object().value(u"tick"_s).toInt() >= 10);
    }

    void worldCommandsCreateSaveAndReload()
    {
        QTemporaryDir data;
        m_process.closeWriteChannel();
        QVERIFY(m_process.waitForFinished(timeoutMs));
        m_process.setArguments({u"--view-distance"_s, u"2"_s, u"--time-limit"_s, u"90"_s, u"--data-dir"_s, data.path()});
        m_process.start();
        QVERIFY(m_process.waitForStarted(timeoutMs));

        const QJsonObject created = ask(R"({"cmd":"worldCreate","name":"Test World","seed":7})");
        QCOMPARE(created.value(u"folder"_s).toString(), u"Test_World"_s);
        QVERIFY(ask(R"({"cmd":"waitUntilReady"})").value(u"ok"_s).toBool());
        QVERIFY(ask(R"({"cmd":"chat","text":"/give bricks 9"})").value(u"ok"_s).toBool());
        QVERIFY(ask(R"({"cmd":"worldSave"})").value(u"ok"_s).toBool());
        QVERIFY(ask(R"({"cmd":"wait","ticks":3})").value(u"ok"_s).toBool());

        // Loading the same folder restarts server and session; the inventory comes from the save.
        QCOMPARE(ask(R"({"cmd":"worldLoad","folder":"Test_World"})").value(u"ok"_s).toBool(), true);
        QVERIFY(ask(R"({"cmd":"waitUntilReady"})").value(u"ok"_s).toBool());
        const QJsonArray stacks = ask(R"({"cmd":"inventory"})").value(u"slots"_s).toArray();
        QCOMPARE(stacks.size(), 1);
        QCOMPARE(stacks.at(0).toObject().value(u"item"_s).toString(), u"qtcraft:bricks"_s);
        QCOMPARE(stacks.at(0).toObject().value(u"count"_s).toInt(), 9);
        const QJsonArray worlds = ask(R"({"cmd":"worldList"})").value(u"worlds"_s).toArray();
        QCOMPARE(worlds.size(), 1);
        QCOMPARE(ask(R"({"cmd":"worldLoad","folder":"../etc"})").value(u"ok"_s).toBool(true), false);

        // A world with a damaged region file fails to load, and the current world keeps working.
        const QString broken = QDir(data.path()).filePath(u"worlds/Broken"_s);
        QVERIFY(QDir().mkpath(broken + u"/regions"_s));
        QVERIFY(QFile::copy(QDir(data.path()).filePath(u"worlds/Test_World/world.json"_s), broken + u"/world.json"_s));
        QFile junk(broken + u"/regions/r.0.0.qcr"_s);
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write("junk junk junk junk");
        junk.close();
        QCOMPARE(ask(R"({"cmd":"worldLoad","folder":"Broken"})").value(u"ok"_s).toBool(true), false);
        QCOMPARE(ask(R"({"cmd":"worldSave"})").value(u"ok"_s).toBool(), true);
        const QJsonObject status = ask(R"({"cmd":"status"})");
        QCOMPARE(status.value(u"state"_s).toString(), u"Playing"_s);
        QCOMPARE(status.value(u"world"_s).toString(), u"Test World"_s);
    }

    void quitEndsTheProcess()
    {
        QCOMPARE(ask(R"({"cmd":"quit"})").value(u"ok"_s).toBool(), true);
        QVERIFY(m_process.waitForFinished(timeoutMs));
        QCOMPARE(m_process.exitCode(), 0);
    }
};

QTEST_GUILESS_MAIN(TestHeadlessCli)
#include "test_headless_cli.moc"
