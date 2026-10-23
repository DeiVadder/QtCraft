#include "clientcore/control/QcControlContext.h"

#include "clientcore/session/QcClientSession.h"

#include <QIODevice>
#include <QJsonDocument>

using namespace Qt::StringLiterals;

QcControlContext::QcControlContext(QcClientSession *session, QIODevice *output)
    : m_session(session)
    , m_output(output)
{
}

void QcControlContext::finish(const QJsonObject &request, QJsonObject response)
{
    if (request.contains(u"id"_s)) {
        response.insert(u"id"_s, request.value(u"id"_s));
    }

    response.insert(u"ok"_s, true);
    write(response);
}

void QcControlContext::fail(const QJsonObject &request, const QString &message)
{
    ++m_failedRequests;
    QJsonObject response{{u"ok"_s, false}, {u"error"_s, message}};

    if (request.contains(u"id"_s)) {
        response.insert(u"id"_s, request.value(u"id"_s));
    }
    if (request.contains(u"cmd"_s)) {
        response.insert(u"cmd"_s, request.value(u"cmd"_s));
    }
    write(response);
}

void QcControlContext::appendChat(const QString &text)
{
    m_chatLog.append(text);
    if (m_chatHistory.size() >= chatHistoryLimit) {
        m_chatHistory.removeFirst();
    }
    m_chatHistory.append(text);
}

void QcControlContext::write(const QJsonObject &response)
{
    if (m_output) {
        m_output->write(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n');
    }
}

void QcControlContext::waitTicks(const QJsonObject &request, quint64 startTick, int ticks, std::function<QJsonObject()> result)
{
    begin({request, QcControlActiveCommand::timeoutPolls(ticks),
           [this, startTick, ticks] { return m_session->tickCount() >= startTick + static_cast<quint64>(ticks); }, std::move(result)});
}
