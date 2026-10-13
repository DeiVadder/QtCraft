#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
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

bool copyDirectory(const QString &from, const QString &to)
{
    QDirIterator it(from, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString source = it.next();
        const QString target = QDir(to).filePath(QDir(from).relativeFilePath(source));
        if (!QDir().mkpath(QFileInfo(target).path()) || !QFile::copy(source, target)) {
            return false;
        }
    }
    return true;
}

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

    void answersHandbookRequests()
    {
        const QJsonObject overview = ask(R"({"cmd":"handbook"})");
        QCOMPARE(overview.value(u"ok"_s).toBool(), true);
        QCOMPARE(overview.value(u"problems"_s).toArray().size(), 0);
        QCOMPARE(overview.value(u"categories"_s).toArray().first().toObject().value(u"id"_s).toString(), u"playing"_s);

        const QJsonObject ore = ask(R"({"cmd":"handbook","entry":"iron_ore"})").value(u"entry"_s).toObject();
        QCOMPARE(ore.value(u"id"_s).toString(), u"block:qtcraft:iron_ore"_s);
        QCOMPARE(ore.value(u"title"_s).toString(), u"Iron ore"_s);
        const QJsonArray facts = ore.value(u"facts"_s).toArray();
        QVERIFY(std::ranges::any_of(facts, [](const QJsonValue &fact) {
            return fact.toObject().value(u"label"_s).toString() == u"Tool"_s
                   && fact.toObject().value(u"value"_s).toString().contains(u"level 2"_s);
        }));

        const QJsonArray found = ask(R"({"cmd":"handbook","search":"Eisenerz","language":"de"})").value(u"results"_s).toArray();
        QVERIFY(!found.isEmpty());
        QCOMPARE(found.first().toObject().value(u"id"_s).toString(), u"block:qtcraft:iron_ore"_s);
        QCOMPARE(found.first().toObject().value(u"title"_s).toString(), u"Eisenerz"_s);

        const QJsonArray creatures = ask(R"({"cmd":"handbook","category":"creatures"})").value(u"entries"_s).toArray();
        // Zombie, pig, slime, firefly, sheep and the two mannequins of the showcase (one with a cape).
        QCOMPARE(creatures.size(), 7);

        const QJsonObject missing = ask(R"({"cmd":"handbook","entry":"no_such_block"})");
        QCOMPARE(missing.value(u"ok"_s).toBool(), false);
        QVERIFY(missing.value(u"error"_s).toString().contains(u"no_such_block"_s));
        QCOMPARE(ask(R"({"cmd":"handbook","category":"nothing"})").value(u"ok"_s).toBool(), false);

        // Only languages of the content get a handbook.
        const QJsonObject klingon = ask(R"({"cmd":"handbook","language":"tlh"})");
        QCOMPARE(klingon.value(u"ok"_s).toBool(), false);
        QVERIFY(klingon.value(u"error"_s).toString().contains(u"de"_s));
    }

    void exportsTheHandbook()
    {
        m_process.closeWriteChannel();
        QVERIFY(m_process.waitForFinished(timeoutMs));
        QTemporaryDir dir;

        // A data directory with the example mod: the export still documents only the base game.
        QTemporaryDir data;
        QVERIFY(copyDirectory(QStringLiteral(QTCRAFT_MODS_SOURCE_DIR), QDir(data.path()).filePath(u"mods"_s)));
        m_process.setArguments({u"--data-dir"_s, data.path(), u"--export-handbook"_s, dir.path(), u"--language"_s, u"de"_s});
        m_process.start();
        QVERIFY(m_process.waitForFinished(timeoutMs));
        QCOMPARE(m_process.exitCode(), 0);
        QVERIFY(m_process.readAllStandardOutput().contains(" entries in 13 files"));
        QFile index(QDir(dir.path()).filePath(u"index.md"_s));
        QVERIFY(index.open(QIODevice::ReadOnly));
        const QByteArray indexText = index.readAll();
        QVERIFY(indexText.startsWith("# QtCraft-Handbuch\n"));
        QVERIFY(indexText.contains("**Inhaltspakete:** QtCraft 0.1.0\n"));
        QVERIFY(QFile::exists(QDir(dir.path()).filePath(u"blocks.md"_s)));
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

    void consoleCommandsThroughTheInterface()
    {
        // The control interface runs console lines with the console's rights (decision E-247); the
        // backup lands next to the folder of worlds and opens like the world.
        QTemporaryDir data;
        m_process.closeWriteChannel();
        QVERIFY(m_process.waitForFinished(timeoutMs));
        m_process.setArguments({u"--view-distance"_s, u"2"_s, u"--time-limit"_s, u"90"_s, u"--data-dir"_s, data.path()});
        m_process.start();
        QVERIFY(m_process.waitForStarted(timeoutMs));

        QVERIFY(ask(R"({"cmd":"worldCreate","name":"Kept","seed":3})").value(u"ok"_s).toBool());
        QVERIFY(ask(R"({"cmd":"waitUntilReady"})").value(u"ok"_s).toBool());
        QCOMPARE(ask(R"({"cmd":"console","text":"op Friend"})").value(u"lines"_s).toArray(), QJsonArray{u"Friend is an operator"_s});
        QCOMPARE(ask(R"({"cmd":"console","text":"/list"})").value(u"lines"_s).toArray(), QJsonArray{u"1 players online: Bot"_s});
        const QJsonArray backup = ask(R"({"cmd":"console","text":"backup first"})").value(u"lines"_s).toArray();
        // Copied in the background (decision E-256): the folder appears when it is complete.
        QVERIFY2(backup.first().toString().startsWith(u"Backup of "_s), qPrintable(backup.first().toString()));
        QCOMPARE(ask(R"({"cmd":"console","text":5})").value(u"ok"_s).toBool(true), false);

        const QDir backupRoot(QDir(data.path()).filePath(u"backups"_s));
        QTRY_COMPARE_WITH_TIMEOUT(backupRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 1, timeoutMs);
        QTRY_COMPARE_WITH_TIMEOUT(backupRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden).size(), 1, timeoutMs);
        const QStringList backups = backupRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        const QString copy = QDir(data.path()).filePath(u"backups/"_s + backups.first());
        QVERIFY(QFile::exists(copy + u"/world.json"_s));
        QVERIFY(QFile::exists(copy + u"/access.json"_s));
        QVERIFY(!QFile::exists(copy + u"/session.lock"_s));

        // Moved into the folder of worlds, the copy loads.
        QVERIFY(QDir().rename(copy, QDir(data.path()).filePath(u"worlds/Restored"_s)));
        QCOMPARE(ask(R"({"cmd":"worldLoad","folder":"Restored"})").value(u"ok"_s).toBool(), true);
        QVERIFY(ask(R"({"cmd":"waitUntilReady"})").value(u"ok"_s).toBool());
        QCOMPARE(ask(R"({"cmd":"console","text":"op"})").value(u"lines"_s).toArray(), QJsonArray{u"Operators: Friend"_s});
    }

    void theServerConsoleReadsStandardInput()
    {
        // --console: no bot joins, every line is a command, stop ends the process (decision E-247).
        m_process.closeWriteChannel();
        QVERIFY(m_process.waitForFinished(timeoutMs));
        m_process.setArguments({u"--console"_s, u"--view-distance"_s, u"2"_s, u"--time-limit"_s, u"60"_s});
        m_process.start();
        QVERIFY(m_process.waitForStarted(timeoutMs));
        m_process.write("list\n/whitelist on\ngive dirt\ntick rate 40\ntick query\nhelp tick\nstop\n");
        QVERIFY(m_process.waitForFinished(timeoutMs));
        QCOMPARE(m_process.exitCode(), 0);

        const QString output = QString::fromUtf8(m_process.readAllStandardOutput());
        QVERIFY(output.startsWith(u"QtCraft server console"_s));
        QVERIFY(output.contains(u"\n0 players online: \n"_s));
        QVERIFY(output.contains(u"\nThe whitelist is on\n"_s));
        QVERIFY(output.contains(u"\nOnly players can use /give\n"_s));
        QVERIFY(output.contains(u"\nTick rate 40.0 per second"_s));
        QVERIFY(output.contains(u"\nUsage: /tick "_s));
        QVERIFY(output.contains(u"\nStopping the server\n"_s));

        // The end of the input also stops it; the time limit only applies when given.
        m_process.setArguments({u"--console"_s, u"--view-distance"_s, u"2"_s});
        m_process.start();
        QVERIFY(m_process.waitForStarted(timeoutMs));
        m_process.write("save\n");
        m_process.closeWriteChannel();
        QVERIFY(m_process.waitForFinished(timeoutMs));
        QCOMPARE(m_process.exitCode(), 0);
        QVERIFY(QString::fromUtf8(m_process.readAllStandardOutput()).contains(u"World saved"_s));

        // A service without input keeps running past the end of it, here until its time limit.
        m_process.setArguments({u"--console"_s, u"--no-eof-stop"_s, u"--view-distance"_s, u"2"_s, u"--time-limit"_s, u"3"_s});
        m_process.start();
        QVERIFY(m_process.waitForStarted(timeoutMs));
        m_process.closeWriteChannel();
        QVERIFY(!m_process.waitForFinished(1500));
        QVERIFY(m_process.waitForFinished(timeoutMs));
        QCOMPARE(m_process.exitCode(), 2);
        QVERIFY(QString::fromUtf8(m_process.readAllStandardOutput()).contains(u"Standard input closed; the server keeps running."_s));
    }

    void showcaseWorldsAndTour()
    {
        QTemporaryDir data;
        m_process.closeWriteChannel();
        QVERIFY(m_process.waitForFinished(timeoutMs));
        m_process.setArguments({u"--view-distance"_s, u"2"_s, u"--time-limit"_s, u"90"_s, u"--data-dir"_s, data.path()});
        m_process.start();
        QVERIFY(m_process.waitForStarted(timeoutMs));

        QCOMPARE(ask(R"({"cmd":"worldCreate","name":"Odd","generator":"spiral"})").value(u"ok"_s).toBool(true), false);
        QCOMPARE(ask(R"({"cmd":"worldCreate","name":"Odd","generator":"flat","options":"3*nothing"})").value(u"ok"_s).toBool(true), false);
        QCOMPARE(ask(R"({"cmd":"worldCreate","name":"Odd","generator":"void","options":"stone"})").value(u"ok"_s).toBool(true), false);
        QVERIFY(!ask(R"({"cmd":"worldCreate","name":"Show","generator":"showcase"})").value(u"folder"_s).toString().isEmpty());
        QVERIFY(ask(R"({"cmd":"waitUntilReady"})").value(u"ok"_s).toBool());
        const QJsonArray worlds = ask(R"({"cmd":"worldList"})").value(u"worlds"_s).toArray();
        QCOMPARE(worlds.size(), 1);
        QCOMPARE(worlds.at(0).toObject().value(u"generator"_s).toString(), u"showcase"_s);
        m_process.closeWriteChannel();
        QVERIFY(m_process.waitForFinished(timeoutMs));

        // The tour: two stops per area, "name x z yaw pitch".
        m_process.setArguments({u"--no-mods"_s, u"--showcase-tour"_s});
        m_process.start();
        QVERIFY(m_process.waitForFinished(timeoutMs));
        QCOMPARE(m_process.exitCode(), 0);
        const QList<QByteArray> lines = m_process.readAllStandardOutput().trimmed().split('\n');
        QVERIFY(lines.size() >= 36);
        for (const QByteArray &line : lines) {
            QCOMPARE(line.split(' ').size(), 5);
        }
        QVERIFY(lines.first().startsWith("demo-light_row "));
    }

    void generatorOptionsOnTheCommandLine()
    {
        m_process.closeWriteChannel();
        QVERIFY(m_process.waitForFinished(timeoutMs));

        // Wrong layers, or options for a type that takes none, end the start like a bad number.
        const QList<QStringList> refused{{u"--generator"_s, u"flat"_s, u"--generator-options"_s, u"3*nothing"_s},
                                         {u"--generator"_s, u"terrain"_s, u"--generator-options"_s, u"stone"_s},
                                         {u"--generator"_s, u"void"_s, u"--generator-options"_s, u"radius=99"_s}};
        for (const QStringList &arguments : refused) {
            m_process.setArguments(arguments + QStringList{u"--time-limit"_s, u"30"_s});
            m_process.start();
            QVERIFY(m_process.waitForFinished(timeoutMs));
            QCOMPARE(m_process.exitCode(), 1);
            QVERIFY2(m_process.readAllStandardError().contains("Invalid --generator-options"), qPrintable(arguments.join(u' ')));
        }

        // Valid layers make the world: the player stands on the fifth layer.
        m_process.setArguments({u"--view-distance"_s, u"2"_s, u"--time-limit"_s, u"60"_s, u"--generator"_s, u"flat"_s,
                                u"--generator-options"_s, u"bedrock,3*dirt,grass_block"_s});
        m_process.start();
        QVERIFY(m_process.waitForStarted(timeoutMs));
        QVERIFY(ask(R"({"cmd":"waitUntilReady"})").value(u"ok"_s).toBool());
        QVERIFY(ask(R"({"cmd":"wait","ticks":20})").value(u"ok"_s).toBool());
        QCOMPARE(ask(R"({"cmd":"status"})").value(u"position"_s).toArray().at(1).toDouble(), 5.0);
    }

    void debugDataThroughTheInterfaceAndTheLog()
    {
        m_process.closeWriteChannel();
        QVERIFY(m_process.waitForFinished(timeoutMs));
        m_process.setArguments({u"--view-distance"_s, u"2"_s, u"--time-limit"_s, u"90"_s, u"--stats"_s, u"200"_s});
        m_process.start();
        QVERIFY(m_process.waitForStarted(timeoutMs));
        QCOMPARE(ask(R"({"cmd":"waitUntilReady"})").value(u"ok"_s).toBool(), true);

        // The same numbers as the debug display: the client's part and, once subscribed, the server's.
        const QJsonObject debug = ask(R"({"cmd":"debug","goals":true})");
        QCOMPARE(debug.value(u"ok"_s).toBool(), true);
        const QJsonObject client = debug.value(u"client"_s).toObject();
        QCOMPARE(client.value(u"protocol"_s).toInt(), 30);
        QCOMPARE(client.value(u"position"_s).toArray().size(), 3);
        QVERIFY(client.value(u"light"_s).toObject().value(u"sky"_s).toInt() >= 0);
        const QJsonObject server = debug.value(u"server"_s).toObject();
        QCOMPARE(server.value(u"worldType"_s).toString(), u"terrain"_s);
        QVERIFY(server.value(u"tickMs"_s).toObject().contains(u"mean"_s));
        const QJsonObject phases = server.value(u"phases"_s).toObject();
        for (const char *phase : {"players", "saving", "chunkLoading", "blockUpdates", "signal", "liquids", "mobs", "items", "plants", "fallingBlocks",
                                  "machines", "streaming", "clientSync", "messages"}) {
            QVERIFY2(phases.value(QLatin1StringView(phase)).toObject().contains(u"max"_s), phase);
        }
        // No frame statistics without a window.
        QVERIFY(!debug.contains(u"frame"_s));

        // The statistics log writes the same JSON to the log (stderr) every 200 ms.
        QByteArray log;
        QVERIFY(QTest::qWaitFor(
            [this, &log] {
                log += m_process.readAllStandardError();
                return log.count("STATS {") >= 2;
            },
            timeoutMs));
        const qsizetype start = log.indexOf("STATS {") + 6;
        const QJsonObject line = QJsonDocument::fromJson(log.mid(start, log.indexOf('\n', start) - start)).object();
        QCOMPARE(line.value(u"client"_s).toObject().value(u"protocol"_s).toInt(), 30);
        QCOMPARE(ask(R"({"cmd":"debug","off":true})").value(u"ok"_s).toBool(), true);

        // The statistics log keeps its share of the subscription: the server's part stays in it.
        log = m_process.readAllStandardError();
        QVERIFY(QTest::qWaitFor(
            [this, &log] {
                log += m_process.readAllStandardError();
                return log.count("STATS {") >= 3 && log.endsWith('\n');
            },
            timeoutMs));
        const qsizetype last = log.lastIndexOf("STATS {") + 6;
        const QJsonObject after = QJsonDocument::fromJson(log.mid(last, log.indexOf('\n', last) - last)).object();
        QVERIFY(after.value(u"server"_s).toObject().contains(u"tickMs"_s));
    }

    void quitEndsTheProcess()
    {
        QCOMPARE(ask(R"({"cmd":"quit"})").value(u"ok"_s).toBool(), true);
        // The process may already be gone when the answer has been read; waitForFinished() is false then.
        QVERIFY(m_process.state() == QProcess::NotRunning || m_process.waitForFinished(timeoutMs));
        QCOMPARE(m_process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(m_process.exitCode(), 0);
    }
};

QTEST_GUILESS_MAIN(TestHeadlessCli)
#include "test_headless_cli.moc"
