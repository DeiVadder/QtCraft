// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/session/QcClientLogin.h"
#include "clientcore/tick/QcSessionTickMode.h"
#include "common/content/mobs/QcAppearance.h"
#include "common/core/QcMailbox.h"
#include "common/protocol/QcSessionMessages.h"
#include "network/QcJoinFailure.h"
#include "network/QcServerAddress.h"
#include "network/QcTcpTransport.h"

#include <QObject>

#include <functional>
#include <memory>
#include <optional>

class QcClientSession;
class QcSessionThread;
class QcTcpConnector;

/*! \brief Joins a network game (decisions E-251, E-252, E-872): connects over TLS to the address,
    checks the server's certificate against the pin in the known servers (pins it at the first
    connection), logs in with the credentials and hands over the session once the server welcomed
    the player.

    Every join ends with exactly one of joined() and failed(); cancel() ends it silently. Lives
    in the GUI thread like the session it makes. A session that ticks on its own thread (decision
    E-560) gets that thread from here: the connector runs in it, so the TLS socket is made, used
    and closed in that one thread and never changes threads (decision E-872); the outcome comes
    back through a QcMailbox, the pin is checked here before anything is sent. */
class QcNetworkClient : public QObject
{
    Q_OBJECT

public:
    /*! How far the join is. */
    enum class Stage {
        Idle,       // no join running
        Connecting, // making the TLS connection
        LoggingIn   // connected, logging in until the server welcomes the player
    };
    Q_ENUM(Stage)

    /*! \brief Whether the server's certificate is checked against its pin.

        Only tests that do not test the pinning turn it off; every program checks. */
    enum class Pinning {
        Check,            // checked (every program)
        DisabledForTests  // not checked (tests of other things)
    };

    /*! \brief What to join with. */
    struct Request
    {
        /*! The server's address. */
        QcServerAddress address;
        /*! The player's name. */
        QString playerName;
        /*! The player's look. */
        QcAppearance appearance;
        /*! The login data. */
        QcClientLogin::Credentials credentials;
        /*! Milliseconds the connection may take before the join fails. */
        int connectTimeoutMs = 10'000;
        /*! How the session ticks (the headless client ticks in its own thread, decision E-560). */
        QcSessionTickMode tickMode = QcSessionTickMode::Threaded;
        /*! The pins (QcKnownServers). With Pinning::Check and TLS a join without it fails. */
        QString knownServersFile;
        /*! Whether the server's certificate is checked against its pin. */
        Pinning pinning = Pinning::Check;
    };

    explicit QcNetworkClient(QObject *parent = nullptr);
    ~QcNetworkClient() override;
    Q_DISABLE_COPY_MOVE(QcNetworkClient)

    /*! Starts joining as \a request says; a join still running is cancelled first. */
    void join(const Request &request);
    /*! Ends the join silently; neither joined() nor failed() follows. */
    void cancel();
    /*! Returns how far the join is. */
    Stage stage() const { return m_stage; }
    /*! Returns the session after joined(): logged in, loading the world, ticking. The caller
        takes it over; null before and once taken. */
    std::unique_ptr<QcClientSession> takeSession();
    /*! Returns the SHA-256 of the certificate of the server of the last join (QcTlsFingerprint),
        once connected. */
    QByteArray serverFingerprint() const { return m_fingerprint; }

signals:
    /*! The join reached \a stage. */
    void stageChanged(QcNetworkClient::Stage stage);
    /*! The server welcomed the player; takeSession() hands over the session. */
    void joined();
    /*! Emitted with the \a failure that ended the join (decision E-964): the step that failed
        (connection, certificate, login), the addresses tried and a translated reason and hint;
        \a refusal is the server's Disconnect (with its translation key) when it sent one. */
    void failed(const QcJoinFailure &failure, const QcMsg::Disconnect &refusal);
    /*! Just before failed(): the server showed another certificate than the one pinned for \a
        address; the join was refused before anything was sent. */
    void certificateChanged(const QString &address);

private:
    // What the connector reported, handed over from its thread.
    struct Outcome
    {
        quint64 attempt = 0;
        // Lives in the connector's thread; null when the connection failed.
        std::unique_ptr<QcTcpTransport> transport;
        QByteArray fingerprint;
        // Why the connection failed (no transport).
        QcJoinFailure failure;
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
    // Logs `failure` and ends the join with failed().
    void fail(const QcJoinFailure &failure, const QcMsg::Disconnect &refusal);
    // Why the session ended while logging in (QcClientSession::ending()).
    QcJoinFailure sessionFailure() const;
    // A failure of `kind` at the joined address with `reason`.
    QcJoinFailure failureOf(QcJoinFailure::Kind kind, const QString &reason) const;

    Request m_request;
    // The thread the session will tick on, from join() until the session takes it.
    std::unique_ptr<QcSessionThread> m_thread;
    // Made, used and deleted in m_thread when there is one, else in this thread.
    std::unique_ptr<QcTcpConnector> m_connector;
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
