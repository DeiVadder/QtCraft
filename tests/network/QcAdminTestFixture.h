#pragma once

#include "QcServerTestFixture.h"

#include <QRegularExpression>

// A server with a raw client that uses the admin channel (decision E-848): requests by kind, the
// answers and reports it got.
class QcAdminTestFixture : public QcServerTestFixture
{
    Q_OBJECT

protected:
    using Kind = QcMsg::AdminRequest::Kind;

    // Sends a request of `client` (the fixture's raw client by default) and returns its answer
    // (std::nullopt when none came).
    std::optional<QcMsg::AdminResult> ask(Kind kind, const QStringList &arguments = {}, RawClient *client = nullptr)
    {
        RawClient &raw = client ? *client : *m_client;
        const quint32 id = ++m_requestId;
        raw.connection.send(QcMsg::AdminRequest{id, kind, arguments});
        QcTestUtil::pump();
        for (const QcMessage &message : std::as_const(raw.admin)) {
            if (const auto *result = std::get_if<QcMsg::AdminResult>(&message); result && result->id == id) {
                return *result;
            }
        }
        return std::nullopt;
    }

    void follow(quint8 topics, quint8 logLevel = 1)
    {
        m_client->connection.send(QcMsg::AdminSubscribe{topics, logLevel});
        QcTestUtil::pump();
    }

    // Every admin message of type T the raw client got, oldest first.
    template<typename T>
    QList<T> received(const RawClient *client = nullptr) const
    {
        QList<T> found;
        for (const QcMessage &message : (client ? client : m_client.get())->admin) {
            if (const auto *typed = std::get_if<T>(&message)) {
                found.append(*typed);
            }
        }
        return found;
    }

    template<typename T>
    std::optional<T> latest(const RawClient *client = nullptr) const
    {
        const QList<T> all = received<T>(client);
        return all.isEmpty() ? std::nullopt : std::optional(all.last());
    }

    // The English lines of an answer.
    static QStringList texts(const QcMsg::AdminResult &result)
    {
        QStringList lines;
        for (const QcMsg::ChatMessage &line : result.lines) {
            lines.append(line.text);
        }
        return lines;
    }

    // The replies of the server console to `line`.
    QStringList console(const QString &line)
    {
        QStringList lines;
        m_server->runConsoleCommand(line, [&lines](const QcMsg::ChatMessage &reply) { lines.append(reply.text); });
        return lines;
    }

    quint32 m_requestId = 0;
};
