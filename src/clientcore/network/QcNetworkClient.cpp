// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/network/QcNetworkClient.h"

#include "clientcore/debug/QcClientLogging.h"
#include "clientcore/network/QcKnownServers.h"
#include "clientcore/session/QcClientSession.h"
#include "clientcore/tick/QcSessionLink.h"
#include "clientcore/tick/QcSessionThread.h"
#include "network/QcTcpConnector.h"
#include "network/tls/QcTlsBackend.h"
#include "network/tls/QcTlsCredentials.h"

#include <tuple>
#include <utility>

using namespace Qt::StringLiterals;

QcNetworkClient::QcNetworkClient(QObject *parent)
    : QObject(parent)
{
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
    m_fingerprint.clear();
    ++m_attempt;
    if (request.tickMode == QcSessionTickMode::Threaded && QcSessionLink::threadWanted()) {
        m_thread = std::make_unique<QcSessionThread>();
        m_thread->start();
    }
    setStage(Stage::Connecting);
    inConnectorThread([this, attempt = m_attempt] { openConnection(attempt); });
}

void QcNetworkClient::cancel()
{
    endAttempt();
    if (m_session) {
        m_session->disconnect(this);
        m_session->disconnectFromServer();
        m_session.release()->deleteLater();
    }
    m_joined = false;
    setStage(Stage::Idle);
}

std::unique_ptr<QcClientSession> QcNetworkClient::takeSession()
{
    if (!m_joined || !m_session) {
        return nullptr;
    }

    m_session->disconnect(this);
    m_joined = false;
    return std::move(m_session);
}

void QcNetworkClient::inConnectorThread(const std::function<void()> &job)
{
    if (m_thread) {
        m_thread->run(job);
    } else {
        job();
    }
}

void QcNetworkClient::openConnection(quint64 attempt)
{
    // In the connector's thread: the socket is made here and stays here.
    m_connector = new QcTcpConnector;
    connect(m_connector, &QcTcpConnector::connected, m_connector, [this, attempt] {
        report({attempt, m_connector->takeTransport(), m_connector->peerFingerprint(), {}});
    });
    connect(m_connector, &QcTcpConnector::failed, m_connector, [this, attempt](const QString &reason) {
        report({attempt, nullptr, {}, reason});
    });
    m_connector->connectTo(m_request.address, m_request.connectTimeoutMs);
}

void QcNetworkClient::report(Outcome &&outcome)
{
    // In the connector's thread; the queued call only wakes this object (tests/tsan.supp).
    if (m_outcomes.push(std::move(outcome))) {
        QMetaObject::invokeMethod(this, &QcNetworkClient::takeOutcomes, Qt::QueuedConnection);
    }
}

void QcNetworkClient::endAttempt(std::unique_ptr<QcTcpTransport> transport)
{
    inConnectorThread([this, &transport] {
        transport.reset();
        delete std::exchange(m_connector, nullptr);
        std::ignore = m_outcomes.takeAll();
    });
    m_thread.reset();
}

void QcNetworkClient::takeOutcomes()
{
    for (Outcome &outcome : m_outcomes.takeAll()) {
        if (outcome.attempt != m_attempt || m_stage != Stage::Connecting) {
            // Not reached: ending an attempt takes its outcomes in their thread.
            Q_ASSERT(!outcome.transport);
            continue;
        }
        if (!outcome.transport) {
            endAttempt();
            fail(outcome.error.isEmpty() ? tr("The connection was lost") : outcome.error, {});
            return;
        }
        onConnected(std::move(outcome));
        return;
    }
}

std::optional<QcNetworkClient::Distrust> QcNetworkClient::checkServer()
{
    // A build without TLS has no certificate to pin.
    if (!QcTlsBackend::enabled || m_request.pinning == Pinning::DisabledForTests) {
        return std::nullopt;
    }
    // Never silently unchecked: a program that forgot the file must not connect blindly.
    if (m_request.knownServersFile.isEmpty() || m_fingerprint.isEmpty()) {
        qCWarning(lcSession).noquote() << u"Join to %1 refused: no file for the known servers or no certificate to check"_s.arg(
            m_request.address.toString());
        return Distrust{tr("QtCraft cannot check the server's certificate (no list of known servers), so it does not connect")};
    }

    QcKnownServers known(m_request.knownServersFile);
    if (const QcStatus loaded = known.load(); !loaded) {
        return Distrust{loaded.error()};
    }
    const QcServerAddress &address = m_request.address;
    switch (known.check(address, m_fingerprint)) {
    case QcKnownServers::Trust::Known:
        return std::nullopt;
    case QcKnownServers::Trust::New:
        qCInfo(lcSession).noquote() << u"First connection to %1: its certificate %2 is pinned"_s.arg(address.toString(),
                                                                                                    QcTlsFingerprint::display(m_fingerprint));
        if (const QcStatus remembered = known.remember(address, m_fingerprint); !remembered) {
            return Distrust{remembered.error()};
        }
        return std::nullopt;
    case QcKnownServers::Trust::Changed:
        break;
    }

    const QByteArray pinned = known.find(address).value_or(QcKnownServers::Entry{}).fingerprint;
    qCWarning(lcSession).noquote() << u"%1 shows the certificate %2 instead of the pinned %3: refused"_s.arg(
        address.toString(), QcTlsFingerprint::display(m_fingerprint), QcTlsFingerprint::display(pinned));
    return Distrust{tr("The server shows another certificate than at your first connection. Someone may be intercepting the "
                       "connection, so QtCraft does not connect. If the server's owner made a new certificate, forget the old one "
                       "and join again.\nPinned: %1\nNow: %2")
                        .arg(QcTlsFingerprint::display(pinned), QcTlsFingerprint::display(m_fingerprint)),
                    true};
}

void QcNetworkClient::onConnected(Outcome &&outcome)
{
    m_fingerprint = outcome.fingerprint;
    // Nothing is sent to a server whose certificate is not trusted.
    if (const std::optional<Distrust> distrust = checkServer()) {
        endAttempt(std::move(outcome.transport));
        if (distrust->changed) {
            emit certificateChanged(m_request.address.toString());
        }
        fail(distrust->reason, {});
        return;
    }

    // The connector is done; the transport stays in its thread, which the session ticks on.
    inConnectorThread([this] { delete std::exchange(m_connector, nullptr); });
    if (m_thread) {
        m_session = std::make_unique<QcClientSession>(std::move(m_thread), std::move(outcome.transport), m_request.playerName);
    } else {
        m_session = std::make_unique<QcClientSession>(std::move(outcome.transport), m_request.playerName);
    }
    m_session->setAppearance(m_request.appearance);
    m_session->setCredentials(m_request.credentials);
    connect(m_session.get(), &QcClientSession::stateChanged, this, &QcNetworkClient::onSessionState);
    setStage(Stage::LoggingIn);
    m_session->start(m_request.tickMode);
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
        m_session.release()->deleteLater();
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
