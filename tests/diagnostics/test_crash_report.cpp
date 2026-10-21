#include "common/diagnostics/QcCrashHandler.h"
#include "common/diagnostics/QcLogFiles.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 60'000;

QString readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

} // namespace

// Crash reports (decision E-834): qtcraft-headless crashes on purpose (--crash-test) and leaves a
// report beside its log naming the cause and the stack (on Windows also a minidump); the next start
// names the report, and a diagnostics bundle carries it.
class TestCrashReport : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_data;

    struct Run
    {
        // Both channels.
        QString output;
        QProcess::ExitStatus status = QProcess::NormalExit;
        // Ended by itself; false when it hung and was killed (a handler that calls itself again).
        bool ended = false;
    };

    // Runs qtcraft-headless with `arguments` in the test's data folder.
    Run run(const QStringList &arguments)
    {
        QProcess process;
        process.setProgram(QStringLiteral(QTCRAFT_HEADLESS_PATH));
        process.setArguments(QStringList{u"--data-dir"_s, m_data.path(), u"--no-mods"_s} + arguments);
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start();
        const bool ended = process.waitForFinished(timeoutMs);
        if (!ended) {
            process.kill();
            process.waitForFinished();
        }
        return {QString::fromUtf8(process.readAll()), process.exitStatus(), ended};
    }

    QDir logs() const { return QDir(QDir(m_data.path()).filePath(QString::fromLatin1(QcLogFiles::folderName))); }

    // The one crash report in the log folder, seen by a later start or not.
    QString report() const
    {
        const QStringList reports = logs().entryList({u"*.crash.txt"_s, u"*.crash-seen.txt"_s}, QDir::Files);
        return reports.size() == 1 ? logs().filePath(reports.first()) : QString();
    }

private slots:
    void initTestCase()
    {
        if (!QcCrashHandler::available()) {
            QSKIP("Sanitizer builds keep their own crash reports (no handler installed)");
        }
    }

    void cleanup()
    {
        QDir(m_data.path()).removeRecursively();
        QDir().mkpath(m_data.path());
    }

    void crashesLeaveAReportWithCauseAndStack_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QString>("cause");
        QTest::addColumn<QString>("reason");
#if defined(Q_OS_WIN)
        QTest::newRow("segv") << u"segv"_s << u"exception: 0xc0000005"_s << QString();
        QTest::newRow("abort") << u"abort"_s << u"exception: abort"_s << QString();
        QTest::newRow("fatal") << u"fatal"_s << u"exception: fatal message"_s << u"reason: crash test"_s;
        QTest::newRow("terminate") << u"terminate"_s << u"exception: abort"_s << u"reason: std::terminate called"_s;
        QTest::newRow("trap") << u"trap"_s << u"exception: 0x80000003"_s << QString();
#else
        QTest::newRow("segv") << u"segv"_s << u"signal: 11 SIGSEGV"_s << QString();
        QTest::newRow("abort") << u"abort"_s << u"SIGABRT"_s << QString();
        QTest::newRow("fatal") << u"fatal"_s << u"SIGABRT"_s << u"reason: crash test"_s;
        QTest::newRow("terminate") << u"terminate"_s << u"SIGABRT"_s << u"reason: std::terminate called"_s;
        // A trap instruction: brk on arm64 (SIGTRAP), ud2 on x86 (SIGILL).
#if defined(Q_PROCESSOR_ARM)
        QTest::newRow("trap") << u"trap"_s << u"signal: 5 SIGTRAP"_s << QString();
#else
        QTest::newRow("trap") << u"trap"_s << u"SIGILL"_s << QString();
#endif
#endif
    }

    void crashesLeaveAReportWithCauseAndStack()
    {
        QFETCH(QString, kind);
        QFETCH(QString, cause);
        QFETCH(QString, reason);

        // --log-file writes the log although no --server runs.
        const QString log = QDir(m_data.path()).filePath(u"run.log"_s);
        const Run crashed = run({u"--crash-test"_s, kind, u"--log-file"_s, log});
        const QString &output = crashed.output;
        QVERIFY2(crashed.ended, qPrintable(output));
#if !defined(Q_OS_WIN)
        // The signal ends the process as it would have without the handler.
        QCOMPARE(crashed.status, QProcess::CrashExit);
#endif

        const QString path = report();
        QVERIFY2(!path.isEmpty(), qPrintable(output + logs().entryList().join(u' ')));
        const QString text = readAll(path);
        QVERIFY2(text.startsWith(u"QtCraft crash report\nqtcraft-headless "_s), qPrintable(text));
        QVERIFY2(text.contains(cause), qPrintable(text));
        QVERIFY2(reason.isEmpty() || text.contains(reason), qPrintable(text));
        QVERIFY2(text.contains(u"thread: "_s), qPrintable(text));
        // At least a few frames of the stack follow its heading.
#if defined(Q_OS_WIN)
        const QString heading = u"stack ("_s;
#else
        const QString heading = u"backtrace:\n"_s;
#endif
        QVERIFY2(text.contains(heading), qPrintable(text));
        const QStringList frames = text.section(heading, 1).split(u'\n', Qt::SkipEmptyParts);
        QVERIFY2(frames.size() >= 4, qPrintable(text));
#if defined(Q_OS_WIN)
        QVERIFY(QFileInfo(path.chopped(QStringLiteral(".crash.txt").size()) + u".dmp"_s).size() > 0);
#else
        // The log ends with the crash and where its report is.
        const QString logText = readAll(log);
        QVERIFY2(logText.contains(u"Crashing on purpose (--crash-test %1)"_s.arg(kind)), qPrintable(logText));
        QVERIFY2(logText.contains(u"*** CRASH: "_s) && logText.contains(QDir::toNativeSeparators(path)), qPrintable(logText));
#endif
    }

    void aCrashOnAnotherThreadNamesThatThread_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QStringList>("causes");
#if defined(Q_OS_WIN)
        QTest::newRow("segv") << u"thread-segv"_s << QStringList{u"exception: 0xc0000005"_s};
        QTest::newRow("overflow") << u"thread-overflow"_s << QStringList{u"exception: 0xc00000fd"_s};
#else
        QTest::newRow("segv") << u"thread-segv"_s << QStringList{u"SIGSEGV"_s, u"SIGBUS"_s};
        // The guard page below the stack: SIGSEGV or SIGBUS, reported from the thread's own signal
        // stack (QcCrashHandler::prepareThread).
        QTest::newRow("overflow") << u"thread-overflow"_s << QStringList{u"SIGSEGV"_s, u"SIGBUS"_s};
#endif
    }

    void aCrashOnAnotherThreadNamesThatThread()
    {
        QFETCH(QString, kind);
        QFETCH(QStringList, causes);

        const QString log = QDir(m_data.path()).filePath(u"thread.log"_s);
        const Run crashed = run({u"--crash-test"_s, kind, u"--log-file"_s, log});
        QVERIFY2(crashed.ended, qPrintable(crashed.output));
        const QString path = report();
        QVERIFY2(!path.isEmpty(), qPrintable(crashed.output + logs().entryList().join(u' ')));
        const QString text = readAll(path);
        QVERIFY2(std::ranges::any_of(causes, [&text](const QString &cause) { return text.contains(cause); }), qPrintable(text));

        // The report names the crashed thread the way the log does, not the main thread.
        const QString logText = readAll(log);
        static const QRegularExpression crashedThread(u"\\[crash test (\\d+)\\] Crashing on purpose on this thread"_s);
        static const QRegularExpression mainThread(u"\\[[^\\]]*?(\\d+)\\] Crashing on purpose \\(--crash-test"_s);
        static const QRegularExpression reported(u"^thread: (\\d+)$"_s, QRegularExpression::MultilineOption);
        const QString worker = crashedThread.match(logText).captured(1);
        const QString main = mainThread.match(logText).captured(1);
        QVERIFY2(!worker.isEmpty() && !main.isEmpty() && worker != main, qPrintable(logText));
        QCOMPARE(reported.match(text).captured(1), worker);
    }

    void theNextStartNamesTheReportAndTheBundleCarriesIt()
    {
        // Without a log file a fatal message still names its reason.
        run({u"--crash-test"_s, u"fatal"_s});
        const QString path = report();
        QVERIFY(!path.isEmpty());
        QVERIFY2(readAll(path).contains(u"reason: crash test"_s), qPrintable(readAll(path)));

        const QString output = run({u"--write-diagnostics"_s}).output;
        const QString seen = report();
        QVERIFY(seen.endsWith(u".crash-seen.txt"_s));
        QVERIFY2(output.contains(u"The previous session crashed; its report: "_s + seen), qPrintable(output));
        QVERIFY2(output.contains(u"Diagnostics written to "_s), qPrintable(output));
        const QDir bundles(QDir(m_data.path()).filePath(u"diagnostics"_s));
        const QStringList folders = bundles.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        QCOMPARE(folders.size(), 1);
        QVERIFY(QFile::exists(QDir(bundles.filePath(folders.first())).filePath(QFileInfo(seen).fileName())));

        // The next start does not name it again, although the start before left no file.
        QVERIFY(!run({u"--write-diagnostics"_s}).output.contains(u"previous session crashed"_s));
    }
};

QTEST_GUILESS_MAIN(TestCrashReport)
#include "test_crash_report.moc"
