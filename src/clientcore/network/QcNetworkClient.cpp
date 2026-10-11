#include "QcNetworkClient.h"

#include "QcClientSession.h"
#include "QcTcpConnector.h"

QcNetworkClient::QcNetworkClient(QObject *parent)
    : QObject(parent)
    , m_connector(new QcTcpConnector(this))
{
    connect(m_connector, &QcTcpConnector::connected, this, &QcNetworkClient::onConnected);
    connect(m_connector, &QcTcpConnector::failed, this, [this](const QString &reason) { fail(reason, {}); });
}

QcNetworkClient::~QcNetworkClient()
{
    cancel();
}

void QcNetworkClient::join(const Request &request)
{
    cancel();
    m_request = request;
    m_joined = false;
    setStage(Stage::Connecting);
    m_connector->connectTo(request.address, request.connectTimeoutMs);
}

void QcNetworkClient::cancel()
{
    m_connector->abort();
    if (m_session && !m_joined) {
        m_session->disconnect(this);
        m_session->disconnectFromServer();
        m_session->deleteLater();
    }
    m_session.clear();
    setStage(Stage::Idle);
}

QcClientSession *QcNetworkClient::takeSession()
{
    if (!m_joined || !m_session) {
        return nullptr;
    }

    QcClientSession *session = m_session;
    session->disconnect(this);
    m_session.clear();
    m_joined = false;
    return session;
}

void QcNetworkClient::onConnected()
{
    std::unique_ptr<QcTcpTransport> transport = m_connector->takeTransport();
    if (!transport) {
        fail(tr("The connection was lost"), {});
        return;
    }

    auto *session = new QcClientSession(std::move(transport), m_request.playerName);
    m_session = session;
    session->setAppearance(m_request.appearance);
    session->setCredentials(m_request.credentials);
    connect(session, &QcClientSession::stateChanged, this, &QcNetworkClient::onSessionState);
    setStage(Stage::LoggingIn);
    session->start();
}

void QcNetworkClient::onSessionState()
{
    // After joined() the session is its taker's business.
    if (!m_session || m_joined) {
        return;
    }

    if (m_session->state() == QcClientSession::State::Disconnected) {
        const QString reason = m_session->disconnectReason();
        const QcMsg::Disconnect refusal = m_session->disconnectMessage();
        m_session->deleteLater();
        m_session.clear();
        fail(reason, refusal);
        return;
    }

    // Welcome: logged in, the world follows.
    if (m_session->state() != QcClientSession::State::Connecting) {
        m_joined = true;
        setStage(Stage::Idle);
        emit joined();
    }
}

void QcNetworkClient::setStage(Stage stage)
{
    if (m_stage != stage) {
        m_stage = stage;
        emit stageChanged(stage);
    }
}

void QcNetworkClient::fail(const QString &reason, const QcMsg::Disconnect &refusal)
{
    setStage(Stage::Idle);
    emit failed(reason, refusal);
}
