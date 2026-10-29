// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDeadlineTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QProcess>
#include <QTest>

// A control-interface bot in its own process: `qtcraft-headless --join` (decision E-270). Requests
// carry an id; send() returns it at once, response() waits for the matching line, request() does
// both. Standard error (join and disconnect messages) is kept for failure reports.
class QcBotProcess
{
public:
    static constexpr int defaultTimeoutMs = 60'000;

    QcBotProcess(QString name, QString dataDirectory)
        : m_name(std::move(name))
        , m_dataDirectory(std::move(dataDirectory))
    {
    }

    ~QcBotProcess() { kill(); }

    QcBotProcess(const QcBotProcess &) = delete;
    QcBotProcess &operator=(const QcBotProcess &) = delete;

    const QString &name() const { return m_name; }
    QProcess &process() { return m_process; }
    QString errorOutput() { return m_errors += QString::fromUtf8(m_process.readAllStandardError()); }

    bool start(const QString &program, quint16 port, const QStringList &extra = {})
    {
        m_responses.clear();
        m_buffer.clear();
        m_errors.clear();
        m_process.setProgram(program);
        m_process.setArguments(QStringList{QStringLiteral("--join"), QStringLiteral("127.0.0.1:%1").arg(port), QStringLiteral("--player-name"),
                                           m_name, QStringLiteral("--data-dir"), m_dataDirectory, QStringLiteral("--no-mods"),
                                           QStringLiteral("--view-distance"), QStringLiteral("2"), QStringLiteral("--time-limit"),
                                           QStringLiteral("600")}
                               + extra);
        m_process.setProcessChannelMode(QProcess::SeparateChannels);
        m_process.start();
        return m_process.waitForStarted(defaultTimeoutMs);
    }

    int send(QJsonObject request)
    {
        const int id = ++m_nextId;
        request.insert(QStringLiteral("id"), id);
        m_process.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
        return id;
    }

    // The response with this id; {"ok":false,"error":…} when it does not come in time or the bot ended.
    QJsonObject response(int id, int timeoutMs = defaultTimeoutMs)
    {
        const QDeadlineTimer deadline(timeoutMs);
        while (!m_responses.contains(id)) {
            if (m_process.state() == QProcess::NotRunning && m_process.bytesAvailable() == 0) {
                return failure(QStringLiteral("bot %1 ended: %2").arg(m_name, errorOutput()));
            }
            if (deadline.hasExpired()) {
                return failure(QStringLiteral("bot %1 did not answer request %2 in time").arg(m_name).arg(id));
            }
            m_process.waitForReadyRead(50);
            QCoreApplication::processEvents();
            readLines();
        }
        return m_responses.take(id);
    }

    QJsonObject request(const QJsonObject &request, int timeoutMs = defaultTimeoutMs) { return response(send(request), timeoutMs); }

    // Asks `request` again every 200 ms until `done` accepts the response; returns the last one.
    template<typename Done>
    QJsonObject poll(const QJsonObject &request, Done done, int timeoutMs = defaultTimeoutMs)
    {
        const QDeadlineTimer deadline(timeoutMs);
        QJsonObject last;
        while (!deadline.hasExpired()) {
            last = this->request(request, timeoutMs);
            if (done(last)) {
                return last;
            }
            QTest::qWait(200);
        }
        last.insert(QStringLiteral("pollTimedOut"), true);
        return last;
    }

    // Asks the bot to quit and waits for the process; its exit code, or -1 when it did not end.
    int quit(int timeoutMs = defaultTimeoutMs)
    {
        send(QJsonObject{{QStringLiteral("cmd"), QStringLiteral("quit")}});
        m_process.closeWriteChannel();
        if (!m_process.waitForFinished(timeoutMs)) {
            return -1;
        }
        return m_process.exitStatus() == QProcess::NormalExit ? m_process.exitCode() : -1;
    }

    void kill()
    {
        if (m_process.state() != QProcess::NotRunning) {
            m_process.kill();
            m_process.waitForFinished(defaultTimeoutMs);
        }
    }

private:
    static QJsonObject failure(const QString &error) { return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}}; }

    void readLines()
    {
        // Standard error is read along, so a chatty bot never blocks on a full pipe.
        m_errors += QString::fromUtf8(m_process.readAllStandardError());
        m_errors = m_errors.right(qsizetype{64} * 1024);
        m_buffer += m_process.readAllStandardOutput();
        for (qsizetype end = m_buffer.indexOf('\n'); end >= 0; end = m_buffer.indexOf('\n')) {
            const QJsonObject line = QJsonDocument::fromJson(m_buffer.left(end)).object();
            m_buffer.remove(0, end + 1);
            if (line.contains(QStringLiteral("id"))) {
                m_responses.insert(line.value(QStringLiteral("id")).toInt(), line);
            }
        }
    }

    QString m_name;
    QString m_dataDirectory;
    QProcess m_process;
    QByteArray m_buffer;
    QString m_errors;
    QHash<int, QJsonObject> m_responses;
    int m_nextId = 0;
};
