#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
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

    void quitEndsTheProcess()
    {
        QCOMPARE(ask(R"({"cmd":"quit"})").value(u"ok"_s).toBool(), true);
        QVERIFY(m_process.waitForFinished(timeoutMs));
        QCOMPARE(m_process.exitCode(), 0);
    }
};

QTEST_GUILESS_MAIN(TestHeadlessCli)
#include "test_headless_cli.moc"
