#pragma once

#include "QcPlayerInput.h"

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QQueue>

#include <functional>

class QcClientSession;
class QIODevice;

// Machine-readable control of a client session: newline-delimited JSON requests in, one JSON
// response line per request out. Acts only through the session (i.e. through the protocol), exactly
// like a player would. Commands are documented in docs/Steuerschnittstelle.md.
class QcControlInterface : public QObject
{
    Q_OBJECT

public:
    // Internal marker of a queued request that failed to parse.
    static constexpr auto parseErrorKey = "__parseError";

    // Optional hook for commands the session cannot serve itself (screenshots, world management).
    using ExtensionHandler = std::function<bool(const QJsonObject &request, QJsonObject &response)>;

    QcControlInterface(QcClientSession *session, QIODevice *input, QIODevice *output, QObject *parent = nullptr);

    void setExtensionHandler(ExtensionHandler handler) { m_extension = std::move(handler); }
    // Executes one request; the response is written to the output device (possibly later).
    void submit(const QJsonObject &request);
    // Appends raw bytes of newline-delimited requests, for sources that are not a QIODevice.
    void feed(const QByteArray &data);
    bool isIdle() const { return !m_active && m_queue.isEmpty(); }
    int failedRequests() const { return m_failedRequests; }

signals:
    void quitRequested();
    void idle();

private:
    struct ActiveCommand
    {
        QJsonObject request;
        // Poll intervals left before the command fails with a timeout.
        int remainingTicks = 0;
        // Called every tick; returns true when the command is complete.
        std::function<bool()> step;
    };

    void readInput();
    void processQueue();
    void start(const QJsonObject &request);
    void finish(const QJsonObject &request, QJsonObject response);
    void fail(const QJsonObject &request, const QString &message);
    void onTick();
    QJsonObject playerStatus() const;

    QPointer<QcClientSession> m_session;
    QPointer<QIODevice> m_input;
    QPointer<QIODevice> m_output;
    ExtensionHandler m_extension;
    QQueue<QJsonObject> m_queue;
    std::optional<ActiveCommand> m_active;
    int m_failedRequests = 0;
    QByteArray m_lineBuffer;
    int m_lineNumber = 0;
};
