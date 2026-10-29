// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/session/QcClientLogin.h"
#include "clientcore/tick/QcSessionTickMode.h"
#include "common/content/definitions/QcAppearance.h"
#include "common/core/QcMailbox.h"
#include "common/protocol/QcSessionMessages.h"
#include "network/QcServerAddress.h"
#include "network/QcTcpTransport.h"

#include <QObject>

#include <functional>
#include <memory>
#include <optional>

class QcClientSession;
class QcSessionThread;
class QcTcpConnector;

// Joins a network game (decisions E-251, E-252, E-872): connects over TLS to the address, checks
// the server's certificate against the pin in the known servers (pins it at the first connection),
// logs in with the credentials and hands over the session once the server welcomed the player.
// Every join ends with exactly one of joined() and failed(); cancel() ends it silently. Lives in
// the GUI thread like the session it makes. A session that ticks on its own thread (decision E-560)
// gets that thread from here: the connector runs in it, so the TLS socket is made, used and closed
// in that one thread and never changes threads (decision E-872); the outcome comes back through a
// QcMailbox, the pin is checked here before anything is sent.
class QcNetworkClient : public QObject
{
    Q_OBJECT

public:
    enum class Stage { Idle, Connecting, LoggingIn };
    Q_ENUM(Stage)

    // Whether the server's certificate is checked against its pin. Only tests that do not test
    // the pinning turn it off; every program checks.
    enum class Pinning { Check, DisabledForTests };

    struct Request
    {
        QcServerAddress address;
        QString playerName;
        QcAppearance appearance;
        QcClientLogin::Credentials credentials;
        int connectTimeoutMs = 10'000;
        // How the session ticks (the headless client ticks in its own thread, decision E-560).
        QcSessionTickMode tickMode = QcSessionTickMode::Threaded;
        // The pins (QcKnownServers). With Pinning::Check and TLS a join without it fails.
        QString knownServersFile;
        Pinning pinning = Pinning::Check;
    };

    explicit QcNetworkClient(QObject *parent = nullptr);
    ~QcNetworkClient() override;
    Q_DISABLE_COPY_MOVE(QcNetworkClient)

    void join(const Request &request);
    void cancel();
    Stage stage() const { return m_stage; }
    // The session after joined(): logged in, loading the world, ticking. The caller
    // takes it over; null before and once taken.
    std::unique_ptr<QcClientSession> takeSession();
    // SHA-256 of the certificate of the server of the last join (QcTlsFingerprint), once connected.
    QByteArray serverFingerprint() const { return m_fingerprint; }

signals:
    void stageChanged(QcNetworkClient::Stage stage);
    void joined();
    // Why the join failed: the connection could not be made or broke, or the server refused the
    // login; `refusal` is the server's Disconnect (with its translation key) when it sent one.
    void failed(const QString &reason, const QcMsg::Disconnect &refusal);
    // Just before failed(): the server showed another certificate than the one pinned for the
    // address; the join was refused before anything was sent.
    void certificateChanged(const QString &address);

private:
    // What the connector reported, handed over from its thread.
    struct Outcome
    {
        quint64 attempt = 0;
        // Lives in the connector's thread; null when the connection failed.
        std::unique_ptr<QcTcpTransport> transport;
        QByteArray fingerprint;
        QString error;
    };
    // Why the server's certificate is not trusted.
    struct Distrust
    {
        QString reason;
        // The server showed another certificate than the pinned one.
        bool changed = false;
    };

    // In the connector's thread.
    void openConnection(quint64 attempt);
    void report(Outcome &&outcome);
    // In this thread.
    void takeOutcomes();
    void onConnected(Outcome &&outcome);
    // Checks the server's certificate against its pin (and pins it at the first connection);
    // nothing when it is trusted.
    std::optional<Distrust> checkServer();
    // Runs `job` in the connector's thread (the session thread when there is one) and waits for it.
    void inConnectorThread(const std::function<void()> &job);
    // Ends the connection attempt: the connector, `transport` and outcomes nobody took go in their
    // thread, then the session thread ends (when the session did not take it).
    void endAttempt(std::unique_ptr<QcTcpTransport> transport = {});
    void onSessionState();
    void setStage(Stage stage);
    void fail(const QString &reason, const QcMsg::Disconnect &refusal);

    Request m_request;
    // The thread the session will tick on, from join() until the session takes it.
    std::unique_ptr<QcSessionThread> m_thread;
    // Made, used and deleted in m_thread when there is one, else in this thread.
    QcTcpConnector *m_connector = nullptr;
    QcMailbox<Outcome> m_outcomes;
    // Counts the joins: an outcome of an earlier one is dropped.
    quint64 m_attempt = 0;
    // Deleted later, not at once, when the join ends: it may be the sender of the signal that
    // ended it.
    std::unique_ptr<QcClientSession> m_session;
    QByteArray m_fingerprint;
    bool m_joined = false;
    Stage m_stage = Stage::Idle;
};
