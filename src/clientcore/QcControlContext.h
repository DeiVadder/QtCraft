#pragma once

#include "QcControlActiveCommand.h"
#include "QcControlReports.h"

#include <QJsonObject>
#include <QPointer>
#include <QStringList>

#include <functional>
#include <optional>
#include <utility>

class QcClientSession;
class QIODevice;

// State the control commands share: the session, the response output, the running command and
// the chat lines received while it runs.
class QcControlContext
{
public:
    QcControlContext(QcClientSession *session, QIODevice *output);

    QcClientSession *session() const { return m_session; }
    void setSession(QcClientSession *session) { m_session = session; }
    // Only valid while a session is set.
    QcControlReports reports() const { return {*m_session, m_chatLog}; }

    void finish(const QJsonObject &request, QJsonObject response);
    void fail(const QJsonObject &request, const QString &message);
    int failedRequests() const { return m_failedRequests; }

    // Runs `command` on the following ticks until it completes.
    void begin(QcControlActiveCommand command) { m_active = std::move(command); }
    // Completes `ticks` client ticks after `startTick` with `result` (the player status when unset).
    void waitTicks(const QJsonObject &request, quint64 startTick, int ticks, std::function<QJsonObject()> result = {});
    std::optional<QcControlActiveCommand> &active() { return m_active; }
    bool isActive() const { return m_active.has_value(); }
    // Set by a step that completes unsuccessfully; the active request then fails with it.
    void setActiveError(const QString &error) { m_activeError = error; }
    QString takeActiveError() { return std::exchange(m_activeError, {}); }

    void appendChat(const QString &text) { m_chatLog.append(text); }
    void clearChatLog() { m_chatLog.clear(); }

private:
    void write(const QJsonObject &response);

    QPointer<QcClientSession> m_session;
    QPointer<QIODevice> m_output;
    std::optional<QcControlActiveCommand> m_active;
    QString m_activeError;
    int m_failedRequests = 0;
    // Chat lines received while the current request runs; reported in its response.
    QStringList m_chatLog;
};
