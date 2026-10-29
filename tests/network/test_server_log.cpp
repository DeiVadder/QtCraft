// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/diagnostics/QcLogFiles.h"
#include "network/QcTcpConnector.h"
#include "QcRawClient.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 30'000;

QString readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

} // namespace

// The log of the dedicated server for a playtest (decision E-834): by default a session log in
// <data>/logs with the build and system, the world and its mods, every connection with the client's
// program and system, the players leaving, and the health line; the console's "diagnostics" writes
// a bundle without the password.
class TestServerLog : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_data;
    QProcess m_process;
    QString m_output;

    quint16 startServer()
    {
        m_process.setProgram(QStringLiteral(QTCRAFT_HEADLESS_PATH));
        m_process.setArguments({u"--server"_s, u"--data-dir"_s, m_data.path(), u"--bind"_s, u"127.0.0.1"_s, u"--port"_s, u"0"_s, u"--no-mods"_s,
                                u"--generator"_s, u"flat"_s, u"--password"_s, u"geheim"_s, u"--console"_s, u"--time-limit"_s, u"600"_s,
                                u"--health-log"_s, u"1"_s});
        m_process.setProcessChannelMode(QProcess::SeparateChannels);
        m_process.start();
        if (!m_process.waitForStarted(timeoutMs)) {
            return 0;
        }
        static const QRegularExpression listening(u"Listening on port (\\d+)"_s);
        quint16 port = 0;
        [[maybe_unused]] const bool up = QTest::qWaitFor([&] {
            m_output += QString::fromUtf8(m_process.readAllStandardOutput());
            const QRegularExpressionMatch match = listening.match(m_output);
            port = match.hasMatch() ? static_cast<quint16>(match.captured(1).toUInt()) : 0;
            return port != 0 || m_process.state() == QProcess::NotRunning;
        }, timeoutMs);
        return port;
    }

    // The server's session log; empty until it exists.
    QString logText() const
    {
        const QDir logs(QDir(m_data.path()).filePath(QString::fromLatin1(QcLogFiles::folderName)));
        const QStringList files = logs.entryList({u"qtcraft-headless-*.log"_s}, QDir::Files);
        return files.size() == 1 ? readAll(logs.filePath(files.first())) : QString();
    }

    bool logContains(const QString &text) const
    {
        return QTest::qWaitFor([&] { return logText().contains(text); }, timeoutMs);
    }

private slots:
    void cleanup()
    {
        if (m_process.state() != QProcess::NotRunning) {
            m_process.kill();
            m_process.waitForFinished(timeoutMs);
        }
    }

    void theServerLogsSessionsPlayersHealthAndDiagnostics()
    {
        const quint16 port = startServer();
        QVERIFY2(port != 0, qPrintable(m_output + QString::fromUtf8(m_process.readAllStandardError())));

        QVERIFY(logContains(u"qtcraft-headless "_s));
        const QString header = logText();
        QVERIFY2(header.contains(u"I qtcraft.diagnostics ["_s) && header.contains(u"System: "_s) && header.contains(u"Processor: "_s), qPrintable(header));
        QVERIFY(header.contains(u"--password <hidden>"_s));
        QVERIFY(!header.contains(u"geheim"_s));
        QVERIFY(header.contains(u"World: World (folder World), seed "_s));
        QVERIFY(header.contains(u"Content packs: qtcraft "_s));
        QVERIFY(header.contains(u"password set"_s));
        // The lines the server prints for people are in the log too.
        QVERIFY(header.contains(u"I qtcraft.headless [") && header.contains(u"Listening on port %1"_s.arg(port)));

        QcTcpConnector connector;
        QSignalSpy connected(&connector, &QcTcpConnector::connected);
        connector.connectTo({u"127.0.0.1"_s, port});
        QVERIFY(connected.wait(timeoutMs));
        auto client = std::make_unique<QcRawClient>(connector.takeTransport());
        client->key = QcTestLogin::keyFor(u"Ann"_s);
        client->password = u"geheim"_s;
        client->connection.send(QcMsg::Hello{QcProtocol::version, u"Ann"_s, {}, u"test-client 1.2 (Windows 11 x86_64)"_s});
        QVERIFY(QTest::qWaitFor([&] { return client->entityId != 0; }, timeoutMs));

        QVERIFY(logContains(u"Hello from 127.0.0.1"_s));
        QVERIFY(logContains(u"player Ann, protocol %1, test-client 1.2 (Windows 11 x86_64)"_s.arg(QcProtocol::version)));
        QVERIFY(logContains(u"Player Ann logged in from 127.0.0.1"_s));
        QVERIFY(logContains(u"players 1"_s));
        QVERIFY2(logText().contains(u"I qtcraft.health ["_s) && logText().contains(u"HEALTH memory "_s), qPrintable(logText()));
        QVERIFY(logText().contains(u"| tick "_s));

        client->connection.close();
        QVERIFY(logContains(u"Player Ann (127.0.0.1"_s));
        QVERIFY(logContains(u" left after "_s));

        m_process.write("diagnostics\nstop\n");
        QVERIFY(m_process.waitForFinished(timeoutMs));
        m_output += QString::fromUtf8(m_process.readAllStandardOutput());
        static const QRegularExpression written(u"Diagnostics written to (\\S+)"_s);
        const QRegularExpressionMatch match = written.match(m_output);
        QVERIFY2(match.hasMatch(), qPrintable(m_output));
        const QDir bundle(match.captured(1));
        const QString server = readAll(bundle.filePath(u"server.json"_s));
        QVERIFY2(server.contains(u"<hidden>"_s) && !server.contains(u"geheim"_s), qPrintable(server));
        QVERIFY(readAll(bundle.filePath(u"info.txt"_s)).contains(u"World: World"_s));
        QVERIFY(!bundle.entryList({u"qtcraft-headless-*.log"_s}, QDir::Files).isEmpty());
        QVERIFY(logContains(u"Session ended"_s));
    }
};

QTEST_GUILESS_MAIN(TestServerLog)
#include "test_server_log.moc"
