#pragma once

#include "clientcore/session/QcClientLogin.h"
#include "clientcore/tick/QcSessionTickMode.h"
#include "common/content/definitions/QcAppearance.h"
#include "common/protocol/QcSessionMessages.h"
#include "network/QcServerAddress.h"

#include <QObject>

#include <memory>

class QcClientSession;
class QcTcpConnector;

// Joins a network game (decisions E-251, E-252): connects to the address, logs in with the
// credentials and hands over the session once the server welcomed the player. Every join ends
// with exactly one of joined() and failed(); cancel() ends it silently. Lives in the GUI thread
// like the session it makes.
class QcNetworkClient : public QObject
{
    Q_OBJECT

public:
    enum class Stage { Idle, Connecting, LoggingIn };
    Q_ENUM(Stage)

    struct Request
    {
        QcServerAddress address;
        QString playerName;
        QcAppearance appearance;
        QcClientLogin::Credentials credentials;
        int connectTimeoutMs = 10'000;
        // How the session ticks (the headless client ticks in its own thread, decision E-560).
        QcSessionTickMode tickMode = QcSessionTickMode::Threaded;
    };

    explicit QcNetworkClient(QObject *parent = nullptr);
    ~QcNetworkClient() override;

    void join(const Request &request);
    void cancel();
    Stage stage() const { return m_stage; }
    // The session after joined(): logged in, loading the world, ticking. The caller
    // takes it over; null before and once taken.
    std::unique_ptr<QcClientSession> takeSession();

signals:
    void stageChanged(QcNetworkClient::Stage stage);
    void joined();
    // Why the join failed: the connection could not be made or broke, or the server refused the
    // login; `refusal` is the server's Disconnect (with its translation key) when it sent one.
    void failed(const QString &reason, const QcMsg::Disconnect &refusal);

private:
    void onConnected();
    void onSessionState();
    void setStage(Stage stage);
    void fail(const QString &reason, const QcMsg::Disconnect &refusal);

    Request m_request;
    QcTcpConnector *m_connector = nullptr;
    // Deleted later, not at once, when the join ends: it may be the sender of the signal that
    // ended it.
    std::unique_ptr<QcClientSession> m_session;
    bool m_joined = false;
    Stage m_stage = Stage::Idle;
};
