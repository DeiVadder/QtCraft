#include "QcControlInterface.h"

#include "QcClientSession.h"
#include "QcControlArguments.h"

#include <QIODevice>
#include <QJsonDocument>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace {

constexpr int pollIntervalMs = 50;

} // namespace

QcControlInterface::QcControlInterface(QcClientSession *session, QIODevice *input, QIODevice *output, QObject *parent)
    : QObject(parent)
    , m_context(session, output)
    , m_commands(m_context)
    , m_input(input)
{
    m_commands.add(u"quit"_s, [this](const QJsonObject &request) {
        m_context.finish(request, {});
        emit quitRequested();
    });

    if (m_input) {
        connect(m_input, &QIODevice::readyRead, this, &QcControlInterface::readInput);
    }

    setSession(session);

    auto *poll = new QTimer(this);
    poll->setInterval(pollIntervalMs);
    connect(poll, &QTimer::timeout, this, &QcControlInterface::onTick);
    poll->start();
}

void QcControlInterface::setSession(QcClientSession *session)
{
    if (const QcClientSession *previous = m_context.session()) {
        disconnect(previous, nullptr, this, nullptr);
    }

    m_context.setSession(session);
    if (session) {
        connect(session, &QcClientSession::chatReceived, this, [this](const QString &text) { m_context.appendChat(text); });
    }
}

void QcControlInterface::readInput()
{
    feed(m_input->readAll());
}

void QcControlInterface::feed(const QByteArray &data)
{
    m_lineBuffer.append(data);
    qsizetype newline = m_lineBuffer.indexOf('\n');

    while (newline >= 0) {
        const QByteArray line = m_lineBuffer.left(newline).trimmed();
        m_lineBuffer.remove(0, newline + 1);
        newline = m_lineBuffer.indexOf('\n');
        ++m_lineNumber;
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }

        QJsonParseError error{};
        const QJsonDocument document = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            // Queued like a request so the error is answered in input order.
            const QString reason = error.error != QJsonParseError::NoError ? error.errorString() : u"not an object"_s;
            submit({{QLatin1StringView(parseErrorKey), u"Invalid JSON request in line %1: %2"_s.arg(m_lineNumber).arg(reason)}});
            continue;
        }

        submit(document.object());
    }
}

void QcControlInterface::submit(const QJsonObject &request)
{
    m_queue.enqueue(request);
    processQueue();
}

void QcControlInterface::processQueue()
{
    while (!m_context.isActive() && !m_queue.isEmpty()) {
        start(m_queue.dequeue());
    }

    if (isIdle()) {
        emit idle();
    }
}

void QcControlInterface::start(const QJsonObject &request)
{
    if (!m_context.session()) {
        m_context.fail(request, u"No session"_s);
        return;
    }

    if (request.contains(QLatin1StringView(parseErrorKey))) {
        m_context.fail({}, request.value(QLatin1StringView(parseErrorKey)).toString());
        return;
    }

    m_context.clearChatLog();

    if (const std::optional<QString> error = QcControlArguments::typeError(request)) {
        m_context.fail(request, *error);
        return;
    }

    const QString command = request.value(u"cmd"_s).toString();
    if (const QcControlCommandTable::Handler *handler = m_commands.find(command)) {
        (*handler)(request);
        return;
    }

    startExtension(request, command);
}

void QcControlInterface::startExtension(const QJsonObject &request, const QString &command)
{
    QJsonObject response;
    if (m_extension && m_extension(request, response)) {
        if (response.value(u"ok"_s).toBool(true)) {
            m_context.finish(request, response);
        } else {
            m_context.fail(request, response.value(u"error"_s).toString());
        }
        return;
    }

    m_context.fail(request, u"Unknown command '%1'"_s.arg(command));
}

void QcControlInterface::onTick()
{
    std::optional<QcControlActiveCommand> &active = m_context.active();
    if (!active) {
        return;
    }

    QcClientSession *session = m_context.session();
    if (!session) {
        abortActive(u"Session ended"_s);
        return;
    }

    if (session->state() == QcClientSession::State::Disconnected) {
        abortActive(u"Disconnected: %1"_s.arg(session->disconnectReason()));
        return;
    }

    if (active->step()) {
        completeActive(*active);
        return;
    }

    if (--active->remainingTicks <= 0) {
        // A timed-out mine command must not keep mining in the background.
        session->setMining(false);
        abortActive(u"Timed out"_s);
    }
}

void QcControlInterface::completeActive(const QcControlActiveCommand &command)
{
    const QJsonObject request = command.request;
    const QString error = m_context.takeActiveError();
    const QJsonObject response = command.result ? command.result() : m_context.reports().playerStatus();
    m_context.active().reset();

    if (error.isEmpty()) {
        m_context.finish(request, response);
    } else {
        m_context.fail(request, error);
    }

    processQueue();
}

void QcControlInterface::abortActive(const QString &message)
{
    std::optional<QcControlActiveCommand> &active = m_context.active();
    if (!active) {
        return;
    }

    const QJsonObject request = active->request;
    active.reset();
    m_context.fail(request, message);
    processQueue();
}
