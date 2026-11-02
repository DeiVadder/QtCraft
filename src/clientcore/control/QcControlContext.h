// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/control/QcControlActiveCommand.h"
#include "clientcore/control/QcControlReports.h"

#include <QJsonObject>
#include <QPointer>
#include <QStringList>

#include <functional>
#include <optional>
#include <utility>

class QcClientSession;
class QIODevice;

/*! \brief State the control commands share: the session, the response output, the running
    command and the chat lines received while it runs. */
class QcControlContext
{
public:
    QcControlContext(QcClientSession *session, QIODevice *output);

    /*! Returns the session the commands act on; null while there is none. */
    QcClientSession *session() const { return m_session; }
    /*! Switches to \a session. */
    void setSession(QcClientSession *session) { m_session = session; }
    /*! Returns the JSON views of the session; only valid while a session is set. */
    QcControlReports reports() const { return {*m_session, m_chatLog}; }

    /*! Writes \a response to \a request as a success (with the request's id). */
    void finish(const QJsonObject &request, QJsonObject response);
    /*! Writes a failure of \a request with the error \a message (with the request's id and
        command). */
    void fail(const QJsonObject &request, const QString &message);
    /*! Returns the number of requests that failed. */
    int failedRequests() const { return m_failedRequests; }

    /*! Runs \a command on the following ticks until it completes. */
    void begin(QcControlActiveCommand command) { m_active = std::move(command); }
    /*! Completes \a request \a ticks client ticks after \a startTick with \a result (the player
        status when unset). */
    void waitTicks(const QJsonObject &request, quint64 startTick, int ticks, std::function<QJsonObject()> result = {});
    /*! Returns the command that runs on the following ticks; nothing while none runs. */
    std::optional<QcControlActiveCommand> &active() { return m_active; }
    /*! Returns whether a command runs on the following ticks. */
    bool isActive() const { return m_active.has_value(); }
    /*! Sets \a error, set by a step that completes unsuccessfully; the active request then fails
        with it. */
    void setActiveError(const QString &error) { m_activeError = error; }
    /*! Returns the error a step set and forgets it; empty for none. */
    QString takeActiveError() { return std::exchange(m_activeError, {}); }

    /*! Lines kept for the chatLog command. */
    static constexpr qsizetype chatHistoryLimit = 200;

    /*! Keeps the received chat line \a text for the running request and the chat history. */
    void appendChat(const QString &text);
    /*! Forgets the chat lines of the running request. */
    void clearChatLog() { m_chatLog.clear(); }
    /*! Returns every chat line received, oldest first, also while no request ran (at most
        chatHistoryLimit). */
    const QStringList &chatHistory() const { return m_chatHistory; }
    /*! Forgets the chat history. */
    void clearChatHistory() { m_chatHistory.clear(); }

private:
    void write(const QJsonObject &response);

    QPointer<QcClientSession> m_session;
    QPointer<QIODevice> m_output;
    std::optional<QcControlActiveCommand> m_active;
    QString m_activeError;
    int m_failedRequests = 0;
    // Chat lines received while the current request runs; reported in its response.
    QStringList m_chatLog;
    QStringList m_chatHistory;
};
