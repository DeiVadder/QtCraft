#include "common/diagnostics/QcMessageLog.h"

#include <QFile>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <thread>
#include <vector>

using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcLogTest, "qtcraft.logtest")

// Messages that reached the handler installed before the log (the console).
std::atomic<int> g_forwarded{0};

void countingHandler(QtMsgType, const QMessageLogContext &, const QString &)
{
    ++g_forwarded;
}

QStringList lines(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll()).split(u'\n', Qt::SkipEmptyParts);
}

} // namespace

// The log file's message handler (decision E-834): the line format, the header, the chain to the
// handler before it, and whole lines from many threads at once.
class TestMessageLog : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QtMessageHandler m_before = nullptr;

private slots:
    void init()
    {
        g_forwarded = 0;
        m_before = qInstallMessageHandler(countingHandler);
    }

    void cleanup()
    {
        QcMessageLog::uninstall();
        qInstallMessageHandler(m_before);
    }

    void aLineCarriesTimesLevelCategoryAndThread()
    {
        const QDateTime now(QDate(2026, 10, 21), QTime(15, 30, 12, 345));
        QCOMPARE(QcMessageLog::formatLine(QtWarningMsg, "qtcraft.server", u"Lag 50 %1"_s, {now, 61'234}, u"main 1a"_s),
                 QByteArray("2026-10-21 15:30:12.345 +61.234 W qtcraft.server [main 1a] Lag 50 %1\n"));
        QCOMPARE(QcMessageLog::formatLine(QtInfoMsg, nullptr, u"x"_s, {now, 0}, u"t"_s).mid(24, 15), QByteArray("+0.000 I defaul"));
    }

    void messagesGoToTheFileAndOnToTheConsole()
    {
        const QString path = m_dir.filePath(u"one.log"_s);
        QVERIFY(QcMessageLog::install({path, {}, {u"Header one"_s, u"Header two"_s}}).has_value());
        QCOMPARE(QcMessageLog::path(), path);
        qCInfo(lcLogTest, "hello %d", 42);
        qCWarning(lcLogTest) << "a warning";
        QcMessageLog::note("qtcraft.headless", u"only in the file"_s);

        const QStringList written = lines(path);
        QCOMPARE(written.size(), 5);
        QVERIFY(written[0].contains(u" I qtcraft.diagnostics ["_s) && written[0].endsWith(u"] Header one"_s));
        QVERIFY(written[2].contains(u" I qtcraft.logtest ["_s) && written[2].endsWith(u"] hello 42"_s));
        QVERIFY(written[3].contains(u" W qtcraft.logtest "_s) && written[3].endsWith(u"a warning"_s));
        QVERIFY(written[4].endsWith(u"only in the file"_s));
        // Every message also reached the console handler; the header and the note did not.
        QCOMPARE(g_forwarded.load(), 2);

        // After uninstalling, messages go only to the console.
        QcMessageLog::uninstall();
        QVERIFY(QcMessageLog::path().isEmpty());
        qCInfo(lcLogTest, "not in the file");
        QCOMPARE(lines(path).size(), 5);
        QCOMPARE(g_forwarded.load(), 3);
    }

    void aDisabledCategoryWritesNothing()
    {
        const QString path = m_dir.filePath(u"rules.log"_s);
        // The header is written whatever the rules say.
        QLoggingCategory::setFilterRules(u"qtcraft.diagnostics.info=false"_s);
        QVERIFY(QcMessageLog::install({path, {}, {u"Header"_s}}).has_value());
        QLoggingCategory::setFilterRules(u"qtcraft.logtest.info=false"_s);
        qCInfo(lcLogTest, "hidden");
        QLoggingCategory::setFilterRules(QString());
        qCInfo(lcLogTest, "shown");
        const QStringList written = lines(path);
        QCOMPARE(written.size(), 2);
        QVERIFY(written[0].endsWith(u"] Header"_s));
        QVERIFY(written[1].endsWith(u"shown"_s));
    }

    void manyThreadsWriteWholeLines()
    {
        const QString path = m_dir.filePath(u"threads.log"_s);
        QVERIFY(QcMessageLog::install({path, {.maxBytes = qint64(64) * 1024, .rotations = 50}, {}}).has_value());
        constexpr int threadCount = 8;
        constexpr int perThread = 400;
        std::vector<std::thread> threads;
        threads.reserve(threadCount);
        for (int t = 0; t < threadCount; ++t) {
            threads.emplace_back([t] {
                for (int i = 0; i < perThread; ++i) {
                    qCInfo(lcLogTest, "thread %d line %d end", t, i);
                }
            });
        }
        for (std::thread &thread : threads) {
            thread.join();
        }
        QcMessageLog::uninstall();

        // The current file and its rotated parts hold every line once and whole.
        static const QRegularExpression whole(u"^\\d{4}-\\d\\d-\\d\\d \\d\\d:\\d\\d:\\d\\d\\.\\d{3} \\+\\d+\\.\\d{3} I qtcraft\\.logtest \\[[^\\]]+\\] "
                                               u"thread (\\d) line (\\d+) end$"_s);
        QSet<QString> seen;
        QStringList all = lines(path);
        for (int part = 1; part <= 50; ++part) {
            all += lines(m_dir.filePath(u"threads.%1.log"_s.arg(part)));
        }
        for (const QString &line : std::as_const(all)) {
            if (line.startsWith(u"(continued from"_s)) {
                continue;
            }
            const QRegularExpressionMatch match = whole.match(line);
            QVERIFY2(match.hasMatch(), qPrintable(line));
            seen.insert(match.captured(1) + u'/' + match.captured(2));
        }
        QCOMPARE(seen.size(), threadCount * perThread);
        QCOMPARE(g_forwarded.load(), threadCount * perThread);
    }

    void recentLinesComeOnceWhileThreadsWrite()
    {
        // The admin channel asks again after the newest line recent() looked at (decision E-848):
        // with threads writing meanwhile, no line may come twice and none out of order.
        QcMessageLog::installWithoutFile();
        constexpr int threadCount = 4;
        constexpr int perThread = 3000;
        std::atomic<int> running{threadCount};
        std::vector<std::thread> threads;
        threads.reserve(threadCount);
        for (int t = 0; t < threadCount; ++t) {
            threads.emplace_back([t, &running] {
                for (int i = 0; i < perThread; ++i) {
                    qCInfo(lcLogTest, "recent %d %d", t, i);
                }
                --running;
            });
        }

        QSet<QString> seen;
        quint64 after = QcMessageLog::latestSequence();
        quint64 previous = after;
        bool duplicate = false;
        bool unordered = false;
        const QSet<QString> categories{u"qtcraft.logtest"_s};
        for (bool last = false; !last;) {
            last = running.load() == 0;
            const QcMessageLog::Recent found = QcMessageLog::recent(0, QcMessageLog::recentLimit, after, categories);
            for (const QcMessageLog::RecentLine &line : found.lines) {
                unordered = unordered || line.sequence <= previous;
                previous = line.sequence;
                duplicate = duplicate || seen.contains(line.text);
                seen.insert(line.text);
            }
            unordered = unordered || found.latest < after;
            after = found.latest;
        }
        for (std::thread &thread : threads) {
            thread.join();
        }
        QVERIFY(!duplicate);
        QVERIFY(!unordered);
        QVERIFY(!seen.isEmpty());
        QCOMPARE(after, QcMessageLog::latestSequence());
    }

    void aFileThatCannotBeWrittenInstallsNothing()
    {
        QFile blocker(m_dir.filePath(u"blocker"_s));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        QVERIFY(!QcMessageLog::install({m_dir.filePath(u"blocker/below.log"_s), {}, {}}).has_value());
        QVERIFY(QcMessageLog::path().isEmpty());
        qCInfo(lcLogTest, "console only");
        QCOMPARE(g_forwarded.load(), 1);
    }
};

QTEST_GUILESS_MAIN(TestMessageLog)
#include "test_message_log.moc"
