#pragma once

#include "clientcore/control/QcControlCommandTable.h"
#include "clientcore/control/QcControlContext.h"
#include "clientcore/input/QcGamepadControls.h"

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QQueue>

#include <functional>

class QcClientSession;
class QIODevice;

// Machine-readable control of a client session: newline-delimited JSON requests in, one JSON
// response line per request out. Acts only through the session (i.e. through the protocol), exactly
// like a player would. Commands are documented in docs/Steuerschnittstelle.md. The virtual pads
// steer the player like a real one in the game (QcGamepadControls, applied once per session tick).
class QcControlInterface : public QObject
{
    Q_OBJECT

public:
    // Internal marker of a queued request that failed to parse.
    static constexpr auto parseErrorKey = "__parseError";

    // Optional hook for commands the session cannot serve itself (screenshots, world management): the
    // response (with "ok": false and "error" for a failure), none for a command it does not know.
    using ExtensionHandler = std::function<std::optional<QJsonObject>(const QJsonObject &request)>;

    QcControlInterface(QcClientSession *session, QIODevice *input, QIODevice *output, QObject *parent = nullptr);

    void setExtensionHandler(ExtensionHandler handler) { m_extension = std::move(handler); }
    // Switches to another session, e.g. after the world changed.
    void setSession(QcClientSession *session);
    // Executes one request; the response is written to the output device (possibly later).
    void submit(const QJsonObject &request);
    // Appends raw bytes of newline-delimited requests, for sources that are not a QIODevice.
    void feed(const QByteArray &data);
    bool isIdle() const { return !m_context.isActive() && m_queue.isEmpty(); }
    int failedRequests() const { return m_context.failedRequests(); }
    // The pads of the gamepad commands (decision E-540); they steer the player (E-541).
    QcVirtualGamepadSource &virtualGamepads() { return m_commands.virtualGamepads(); }
    QcGamepadControls &gamepadControls() { return m_gamepad; }

signals:
    void quitRequested();
    void idle();

private:
    void readInput();
    void processQueue();
    void start(const QJsonObject &request);
    // Commands outside the table: the extension handler, else an unknown command.
    void startExtension(const QJsonObject &request, const QString &command);
    void onTick();
    // Answers the completed active command with its result or the error its step set.
    void completeActive(const QcControlActiveCommand &command);
    // Fails the active command with `message`.
    void abortActive(const QString &message);

    QcControlContext m_context;
    QcControlCommandTable m_commands;
    // Follows the pads of m_commands, so it is declared after it.
    QcGamepadControls m_gamepad;
    QPointer<QIODevice> m_input;
    ExtensionHandler m_extension;
    QQueue<QJsonObject> m_queue;
    QByteArray m_lineBuffer;
    int m_lineNumber = 0;
};
