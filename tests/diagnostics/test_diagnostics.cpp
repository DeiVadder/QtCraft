#include "clientcore/tick/QcLinkStats.h"
#include "common/diagnostics/QcBuildInfo.h"
#include "common/diagnostics/QcDiagnosticsBundle.h"
#include "common/diagnostics/QcDiagnosticsOptions.h"
#include "common/diagnostics/QcDiagnosticsSession.h"
#include "common/diagnostics/QcHealthLog.h"
#include "common/diagnostics/QcLogFiles.h"
#include "common/diagnostics/QcLogWriter.h"
#include "common/protocol/QcProtocol.h"
#include "QcTestProtocol.h"
#include "server/tick/QcLagReport.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>

using namespace Qt::StringLiterals;

namespace {

void touch(const QDir &dir, const QString &name, const QByteArray &content = "x")
{
    QFile file(dir.filePath(name));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

QString readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

// The phases of one tick in milliseconds, all others 0.
QcTickPhaseTimer::Durations phases(std::initializer_list<std::pair<QcTickPhase, double>> times)
{
    QcTickPhaseTimer::Durations durations{};
    for (const auto &[phase, ms] : times) {
        durations[std::size_t(phase)] = qint64(ms * 1e6);
    }
    return durations;
}

// The clock of a QcTickPhaseTimer under test.
std::atomic<qint64> g_fakeNs{0};
qint64 fakeClock()
{
    return g_fakeNs.load();
}

} // namespace

// The diagnostics of a playtest (decision E-834): file names and rotation of the logs, the health
// and lag lines, the bundle without secrets, the options and the client text in Hello.
class TestDiagnostics : public QObject
{
    Q_OBJECT

private slots:
    void sessionsAreNamedByStartAndPrunedOldestFirst()
    {
        const QTemporaryDir temp;
        const QDir dir(temp.path());
        const QDateTime start(QDate(2026, 10, 21), QTime(15, 30, 12));
        const QString first = QcLogFiles::freeSessionName(dir, u"qtcraft"_s, start);
        QCOMPARE(first, u"qtcraft-20261021-153012"_s);
        touch(dir, QcLogFiles::logName(first));
        // A second start in the same second, and the other program's files, do not collide.
        const QString second = QcLogFiles::freeSessionName(dir, u"qtcraft"_s, start);
        QCOMPARE(second, u"qtcraft-20261021-153012-2"_s);
        touch(dir, QcLogFiles::logName(second));
        touch(dir, QcLogFiles::crashReportName(second));
        touch(dir, u"qtcraft-headless-20261021-100000.log"_s);
        touch(dir, u"notes.txt"_s);
        for (int minute = 10; minute < 20; ++minute) {
            const QString session = u"qtcraft-20261020-12%1%2"_s.arg(minute).arg(u"00"_s);
            touch(dir, QcLogFiles::logName(session));
            touch(dir, QcLogFiles::dumpName(session));
        }

        const QStringList sessions = QcLogFiles::sessions(dir, u"qtcraft"_s);
        QCOMPARE(sessions.size(), 12);
        QCOMPARE(sessions.first(), u"qtcraft-20261020-121000"_s);
        QCOMPARE(sessions.last(), second);
        QCOMPARE(QcLogFiles::sessions(dir, u"qtcraft-headless"_s), QStringList{u"qtcraft-headless-20261021-100000"_s});

        // The crash of the session before the current one is found, not older ones.
        const QString third = QcLogFiles::freeSessionName(dir, u"qtcraft"_s, start.addSecs(60));
        QCOMPARE(QcLogFiles::previousCrashReport(dir, u"qtcraft"_s, third), dir.filePath(QcLogFiles::crashReportName(second)));
        QCOMPARE(QcLogFiles::previousCrashReport(dir, u"qtcraft"_s, second), QString());
        // Once named, a report is not named again.
        const QString seen = QcLogFiles::markSeen(dir.filePath(QcLogFiles::crashReportName(second)));
        QCOMPARE(seen, dir.filePath(QcLogFiles::seenCrashReportName(second)));
        QVERIFY(QFile::exists(seen));
        QCOMPARE(QcLogFiles::previousCrashReport(dir, u"qtcraft"_s, third), QString());

        const QStringList deleted = QcLogFiles::prune(dir, u"qtcraft"_s, 3);
        QCOMPARE(deleted.size(), 18);
        QCOMPARE(QcLogFiles::sessions(dir, u"qtcraft"_s), (QStringList{u"qtcraft-20261020-121900"_s, first, second}));
        QVERIFY(dir.exists(u"notes.txt"_s));
        QVERIFY(dir.exists(u"qtcraft-headless-20261021-100000.log"_s));
        QVERIFY(dir.exists(QcLogFiles::seenCrashReportName(second)));
    }

    void theLogRotatesAtItsSizeCap()
    {
        const QTemporaryDir temp;
        const QString path = QDir(temp.path()).filePath(u"sub/qtcraft-20261021-153012.log"_s);
        // Room for two lines besides the line naming the previous part, whose length is the path's.
        const qint64 cap = qint64(3) * 40 + QcLogFiles::rotatedPath(path, 1).toUtf8().size();
        QcLogWriter writer(path, {.maxBytes = cap, .rotations = 2});
        QVERIFY(cap < qint64(30) * 40);
        QVERIFY(writer.open().has_value());
        for (int i = 0; i < 30; ++i) {
            writer.write(u"line %1 %2\n"_s.arg(i, 2, 10, QLatin1Char('0')).arg(QString(30, u'a')).toUtf8());
        }

        // The current file and two parts within the cap, a part beyond them is gone; together they
        // hold the latest lines in order, each new file says where the log continues from.
        QVERIFY(writer.rotationCount() >= 3);
        const QString part1 = QcLogFiles::rotatedPath(path, 1);
        const QString part2 = QcLogFiles::rotatedPath(path, 2);
        QCOMPARE(QFileInfo(part1).fileName(), u"qtcraft-20261021-153012.1.log"_s);
        for (const QString &file : {path, part1, part2}) {
            QVERIFY2(QFileInfo(file).size() > 0 && QFileInfo(file).size() <= cap, qPrintable(file));
        }
        QVERIFY(!QFile::exists(QcLogFiles::rotatedPath(path, 3)));
        QVERIFY(readAll(path).startsWith(u"(continued from "_s + part1));
        const QString joined = readAll(part2) + readAll(part1) + readAll(path);
        QVERIFY(joined.endsWith(u"line 29 "_s + QString(30, u'a') + u'\n'));
        QVERIFY(joined.indexOf(u"line 27"_s) < joined.indexOf(u"line 28"_s));
        QVERIFY(!joined.contains(u"line 00"_s));
        // Rotated parts belong to the session: pruning takes them along.
        QCOMPARE(QcLogFiles::sessions(QDir(QFileInfo(path).path()), u"qtcraft"_s).size(), 1);
    }

    void aLogFileThatCannotBeOpenedIsAnError()
    {
        const QTemporaryDir temp;
        touch(QDir(temp.path()), u"file"_s);
        QcLogWriter writer(QDir(temp.path()).filePath(u"file/below/a/file.log"_s), {});
        QVERIFY(!writer.open().has_value());
        writer.write("ignored\n");
    }

    void theHealthLineNamesWhatIsKnown()
    {
        QcHealthSample sample;
        QCOMPARE(QcHealthLog::line(sample, {512.4, 600.0}, std::nullopt, 60.0), u"HEALTH memory 512 MiB (peak 600 MiB)"_s);

        sample.frames = QcIntervalTiming{16.0, 40.25, 3600};
        sample.ticks = QcIntervalTiming{3.25, 12.0, 1200};
        sample.roundTrip = QcIntervalTiming{54.0, 80.0, 1200};
        sample.loadedChunks = 441;
        sample.entities = 37;
        sample.players = 2;
        sample.traffic = QcTraffic{quint64(1024) * 1000 + 61440, 2048};
        const QString line = QcHealthLog::line(sample, {512.0, 600.0}, QcTraffic{quint64(1024) * 1000, 0}, 60.0);
        QCOMPARE(line, u"HEALTH memory 512 MiB (peak 600 MiB) | frames 62.5 fps, 16.0 ms mean, 40.3 ms max | tick 3.3 ms mean, 12.0 ms max"
                       u" | chunks 441 | entities 37 | players 2 | round trip 54.0 ms mean, 80.0 ms max | network out 1.0 KiB/s, in 0.0 KiB/s"_s);
        // A new connection counts from zero: no negative rates.
        QVERIFY(QcHealthLog::line(sample, {}, QcTraffic{1u << 30, 1u << 30}, 60.0).contains(u"out 0.0 KiB/s, in 0.0 KiB/s"_s));
    }

    void intervalStatsStartAnewWhenTaken()
    {
        QcIntervalStats stats;
        QVERIFY(!stats.take());
        stats.add(10.0);
        stats.add(30.0);
        const std::optional<QcIntervalTiming> taken = stats.take();
        QVERIFY(taken);
        QCOMPARE(taken->meanMs, 20.0);
        QCOMPARE(taken->maxMs, 30.0);
        QCOMPARE(taken->count, quint64(2));
        QVERIFY(!stats.take());
    }

    void aHangingTickIsNamedWithTheLastPhaseThatFinished()
    {
        constexpr qint64 second = 1'000'000'000;
        QcTickPhaseTimer timer(fakeClock);
        g_fakeNs = 0;
        QVERIFY(!timer.stall(10 * second));
        timer.beginTick();
        QVERIFY(!timer.stall(10 * second));
        g_fakeNs = second;
        timer.lap(QcTickPhase::Players);
        g_fakeNs = 9 * second;
        QVERIFY(!timer.stall(10 * second));
        g_fakeNs = 12 * second;
        QCOMPARE(timer.stall(10 * second), u"Server tick running for 12 s, the last phase that finished: players"_s);
        // Between ticks the server waits on purpose.
        timer.endTick(0);
        g_fakeNs = 60 * second;
        QVERIFY(!timer.stall(10 * second));
    }

    void lagSpikesAreReportedWithTheirPhasesAtMostEveryTenSeconds()
    {
        QcLagReport lag;
        constexpr qint64 second = 1'000'000'000;
        QVERIFY(!lag.check(phases({{QcTickPhase::Mobs, 30.0}, {QcTickPhase::Liquids, 20.0}}), 0));
        // Message time between ticks is no part of the tick.
        QVERIFY(!lag.check(phases({{QcTickPhase::Mobs, 30.0}, {QcTickPhase::Messages, 90.0}}), 0));

        const std::optional<QString> first =
            lag.check(phases({{QcTickPhase::ChunkLoading, 40.0}, {QcTickPhase::Mobs, 20.5}, {QcTickPhase::Liquids, 0.5}}), second);
        QCOMPARE(first, u"Lag spike: tick took 61.0 ms (chunkLoading 40.0, mobs 20.5, liquids 0.5)"_s);
        QVERIFY(!lag.check(phases({{QcTickPhase::Streaming, 120.0}}), 2 * second));
        QVERIFY(!lag.check(phases({{QcTickPhase::Streaming, 80.0}}), 3 * second));
        const std::optional<QString> later = lag.check(phases({{QcTickPhase::Items, 51.0}}), 11 * second);
        QCOMPARE(later, u"Lag spike: tick took 51.0 ms (items 51.0); 2 more since the previous line, the longest 120.0 ms"_s);
        // Spikes counted after the last line are named when the server stops.
        QVERIFY(!lag.flush());
        QVERIFY(!lag.check(phases({{QcTickPhase::Mobs, 70.0}}), 12 * second));
        QCOMPARE(lag.flush(), u"Lag spikes: 1 more since the previous line, the longest 70.0 ms"_s);
        QVERIFY(!lag.flush());

        // At most five phases, the longest first.
        QcTickPhaseTimer::Durations many{};
        for (int phase = 0; phase < QcTickPhases::inTick; ++phase) {
            many[std::size_t(phase)] = qint64(phase + 1) * 1'000'000;
        }
        const std::optional<QString> crowded = lag.check(many, 30 * second);
        QVERIFY(crowded);
        QCOMPARE(crowded->count(u", "_s), QcLagReport::shownPhases - 1);
        QVERIFY2(crowded->contains(u"(clientSync 18.0, streaming 17.0"_s), qPrintable(*crowded));
    }

    void inputRoundTripsAreMeasuredFromSendToAcknowledgement()
    {
        QcLinkStats stats;
        constexpr qint64 ms = 1'000'000;
        stats.inputSent(1, 0);
        stats.inputSent(2, 50 * ms);
        stats.inputSent(3, 100 * ms);
        stats.acknowledged(2, 130 * ms);
        // The same acknowledgement again, an unknown and a forgotten input count nothing.
        stats.acknowledged(2, 400 * ms);
        stats.acknowledged(99, 400 * ms);
        stats.acknowledged(0, 400 * ms);
        stats.acknowledged(3, 200 * ms);
        stats.setTraffic({100, 200});

        const QcLinkStats::Taken taken = stats.take();
        QVERIFY(taken.roundTrip);
        QCOMPARE(taken.roundTrip->count, quint64(2));
        QCOMPARE(taken.roundTrip->meanMs, 90.0);
        QCOMPARE(taken.roundTrip->maxMs, 100.0);
        QCOMPARE(taken.traffic.received, quint64(200));
        QVERIFY(!stats.take().roundTrip);

        for (quint32 sequence = 10; sequence < 10 + QcLinkStats::remembered + 1; ++sequence) {
            stats.inputSent(sequence, qint64(sequence) * ms);
        }
        stats.acknowledged(10, 500 * ms);
        QVERIFY(!stats.take().roundTrip);
    }

    void secretsStayOutOfTheBundle()
    {
        const QByteArray ini = "[multiplayer]\nplayerName=Ann\nhostPassword=geheim\napiToken = abc\n[keys]\nforward=W\n";
        const QString cleaned = QString::fromUtf8(QcDiagnosticsBundle::cleanSettings(ini));
        QVERIFY(!cleaned.contains(u"geheim"_s));
        QVERIFY(!cleaned.contains(u"abc"_s));
        QVERIFY(cleaned.contains(u"playerName=Ann"_s));
        QVERIFY(cleaned.contains(u"forward=W"_s));

        const QByteArray json = R"({"password": "geheim", "port": 25600, "nested": [{"secretKey": "x"}], "motd": "Hi"})";
        const QJsonObject server = QJsonDocument::fromJson(QcDiagnosticsBundle::cleanServerSettings(json)).object();
        QCOMPARE(server.value(u"password"_s).toString(), u"<hidden>"_s);
        QCOMPARE(server.value(u"port"_s).toInt(), 25600);
        QCOMPARE(server.value(u"nested"_s).toArray().first().toObject().value(u"secretKey"_s).toString(), u"<hidden>"_s);
        QVERIFY(QcDiagnosticsBundle::cleanServerSettings("{ broken").startsWith("(server.json"));

        QCOMPARE(QcDiagnosticsSession::withoutSecrets({u"qtcraft"_s, u"--password"_s, u"geheim"_s, u"--password=x"_s, u"--join"_s, u"host"_s}),
                 (QStringList{u"qtcraft"_s, u"--password"_s, u"<hidden>"_s, u"--password=<hidden>"_s, u"--join"_s, u"host"_s}));
    }

    void theBundleHoldsLogsAndCleanSettingsOnly()
    {
        const QTemporaryDir temp;
        QDir data(temp.path());
        QVERIFY(data.mkpath(u"logs"_s) && data.mkpath(u"mods/fancy"_s) && data.mkpath(u"worlds/Island"_s));
        const QDir logs(data.filePath(u"logs"_s));
        // Four sessions: the logs of the newest three go along, the crash reports of all.
        touch(logs, u"qtcraft-20261021-153012.log"_s, "a log line\n");
        touch(logs, u"qtcraft-20261021-153012.crash.txt"_s, "signal: 11\n");
        for (const QString &session : {u"qtcraft-20261021-160000"_s, u"qtcraft-20261021-170000"_s, u"qtcraft-20261021-180000"_s}) {
            touch(logs, session + u".log"_s);
        }
        touch(logs, u"qtcraft-20261021-180000.1.log"_s);
        touch(logs, u"qtcraft-headless-20261020-090000.log"_s);
        touch(data, u"settings.ini"_s, "[multiplayer]\nhostPassword=geheim\n");
        touch(data, u"server.json"_s, R"({"password": "geheim"})");
        touch(data, u"player.key"_s, "0123456789abcdef");
        touch(data, u"identities.json"_s, "{}");

        const QcResult<QString> written = QcDiagnosticsBundle::write({u"qtcraft"_s, temp.path(), {u"World: Island"_s}});
        QVERIFY2(written, qPrintable(written ? QString() : written.error()));
        QVERIFY(written->startsWith(data.filePath(u"diagnostics/qtcraft-"_s)));
        const QDir bundle(*written);
        QCOMPARE(bundle.entryList(QDir::Files),
                 (QStringList{u"info.txt"_s, u"qtcraft-20261021-153012.crash.txt"_s, u"qtcraft-20261021-160000.log"_s,
                              u"qtcraft-20261021-170000.log"_s, u"qtcraft-20261021-180000.1.log"_s, u"qtcraft-20261021-180000.log"_s,
                              u"qtcraft-headless-20261020-090000.log"_s, u"server.json"_s, u"settings.ini"_s}));
        const QStringList files = bundle.entryList(QDir::Files);
        for (const QString &name : files) {
            QVERIFY2(!readAll(bundle.filePath(name)).contains(u"geheim"_s), qPrintable(name));
        }
        const QString info = readAll(bundle.filePath(u"info.txt"_s));
        QVERIFY(info.contains(u"Mods ("_s) && info.contains(u"fancy"_s));
        QVERIFY(info.contains(u"Island"_s));
        QVERIFY(info.contains(QcBuildInfo::describe(u"qtcraft"_s)));
        QVERIFY2(info.contains(u"Older log files left out: 1"_s), qPrintable(info));

        // A second bundle in the same second gets a folder of its own.
        const QcResult<QString> again = QcDiagnosticsBundle::write({u"qtcraft"_s, temp.path(), {}});
        QVERIFY(again && *again != *written);
    }

    void optionsAreCheckedAndHelpHidesTheCrashTest()
    {
        const QcDiagnosticsOptions options;
        const auto read = [&options](const QStringList &arguments) {
            QCommandLineParser parser;
            parser.addOptions(options.all());
            [[maybe_unused]] const bool parsed = parser.parse(QStringList{u"qtcraft"_s} + arguments);
            return options.read(parser);
        };

        const QcResult<QcDiagnosticsSetup> setup =
            read({u"--log-file"_s, u"x.log"_s, u"--health-log"_s, u"30"_s, u"--log-rules"_s, u"qtcraft.network.debug=true"_s, u"--full-dump"_s});
        QVERIFY(setup);
        QCOMPARE(setup->logFile, u"x.log"_s);
        QCOMPARE(setup->healthSeconds, 30);
        QVERIFY(setup->fullDump);
        QCOMPARE(setup->logRules, u"qtcraft.network.debug=true"_s);
        QVERIFY(!read({}).value().healthSeconds);
        QVERIFY(!read({u"--health-log"_s, u"-1"_s}));
        QVERIFY(!read({u"--health-log"_s, u"often"_s}));
        QVERIFY(!read({u"--crash-test"_s, u"explode"_s}));
        QCOMPARE(read({u"--crash-test"_s, u"segv"_s}).value().crashTest, u"segv"_s);

        QCommandLineParser parser;
        parser.addOptions(options.all());
        QVERIFY(!parser.helpText().contains(u"crash-test"_s));
        QVERIFY(parser.helpText().contains(u"--health-log"_s));
    }

    void helloCarriesTheClientTextAcrossVersions()
    {
        const QString client = QcBuildInfo::clientText(u"qtcraft"_s);
        QVERIFY(client.startsWith(u"qtcraft "_s + QcBuildInfo::version()));
        QVERIFY(client.size() <= QcMsg::Hello::maxClientLength);

        const auto decoded = [](const QcMsg::Hello &hello) {
            return QcProtocol::decode(QcProtocol::encode(hello), QcProtocol::Peer::Client);
        };
        const QcResult<QcMessage> same = decoded(QcMsg::Hello{QcProtocol::version, u"Ann"_s, {}, client});
        QVERIFY(same);
        QCOMPARE(std::get<QcMsg::Hello>(*same).client, client);
        // A newer version keeps the first fields: the server still logs who knocked.
        const QcResult<QcMessage> newer = decoded(QcMsg::Hello{QcProtocol::version + 1, u"Ann"_s, {}, client});
        QVERIFY(newer);
        QCOMPARE(std::get<QcMsg::Hello>(*newer).client, client);
        QVERIFY(!decoded(QcMsg::Hello{QcProtocol::version, u"Ann"_s, {}, QString(QcMsg::Hello::maxClientLength + 1, u'x')}));

        // A real client of the version before the client text (version, name, appearance): its name
        // is read, the rest is skipped, so the server can name the version mismatch.
        const QcResult<QcMessage> old = QcProtocol::decode(QcTestProtocol::helloWithoutClientText(u"Old"_s), QcProtocol::Peer::Client);
        QVERIFY2(old, qPrintable(old ? QString() : old.error()));
        QCOMPARE(std::get<QcMsg::Hello>(*old).protocolVersion, QcProtocol::firstVersionWithClientText - 1);
        QCOMPARE(std::get<QcMsg::Hello>(*old).playerName, u"Old"_s);
        QVERIFY(std::get<QcMsg::Hello>(*old).client.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestDiagnostics)
#include "test_diagnostics.moc"
