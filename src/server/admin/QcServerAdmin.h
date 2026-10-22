#pragma once

#include "common/protocol/QcConnection.h"
#include "common/protocol/QcMessages.h"
#include "server/admin/QcAdminReports.h"
#include "server/admin/QcAdminStatusRecorder.h"

#include <QElapsedTimer>
#include <QHash>

class QcServerCommands;
struct QcServerClient;

// The server's side of the admin channel (decision E-848): operators who play on the server follow
// its status, players, chat and log and act on it over their game connection. The rights are
// checked on every message and every tick: a player who is no operator (or no longer one) is
// refused with a warning in the log, and his subscription ends at once. Every action runs as the
// operator's command line through QcServerCommands, the same code as the chat and the console, so
// the admin channel adds no rules of its own; stopping the server runs the console's /stop and
// needs the argument "confirm". The status history is recorded all the time; pings to measure the
// players' round trips go out only while an operator follows the status.
class QcServerAdmin
{
public:
    // Status and log lines go out once a second of the players' time, pings every five seconds.
    static constexpr qint64 statusIntervalNs = 1'000'000'000;
    static constexpr qint64 pingIntervalNs = 5'000'000'000;
    // Log lines in a tail at most.
    static constexpr qsizetype tailLines = 500;

    QcServerAdmin(QcServerState &state, QcServerCommands &commands, const QcTickPhaseTimer &phases, const QcTickScheduler &ticks);

    // An admin message or a Pong of a playing client; false for every other message.
    bool handle(QcServerClient &client, const QcMessage &message);
    // A tick ended after `ms` (the phases inside the tick).
    void noteTick(double ms) { m_recorder.noteTick(ms); }
    // Every tick: rights of the subscribers, new chat lines, and once a second the status sample,
    // the status, the players and new log lines for those who follow them.
    void update();
    // A client left: his network bytes count on (traffic()), his subscription ends.
    void noteLeft(const QcServerClient &client);
    // Bytes of all network clients so far, those who left included.
    QcTraffic traffic() const;
    const QcAdminStatusRecorder &recorder() const { return m_recorder; }

private:
    struct Subscriber
    {
        quint8 topics = 0;
        QcMsg::AdminLogLine::Level logLevel = QcMsg::AdminLogLine::Level::Info;
        quint64 chatSeen = 0;
        quint64 logSeen = 0;
    };
    struct Ping
    {
        quint32 nonce = 0;
        QElapsedTimer sent;
        bool waiting = false;
        qint32 lastMs = -1;
    };

    // Whether the client may use the admin channel; refuses (`id`) and logs it when he may not.
    bool authorized(QcServerClient &client, quint32 id, QLatin1StringView what);
    void subscribe(QcServerClient &client, QcMsg::AdminSubscribe subscribe);
    void request(QcServerClient &client, const QcMsg::AdminRequest &request);
    // The log's tail, at most once a second per client.
    void logTail(QcServerClient &client, const QcMsg::AdminRequest &request);
    // Requests answered with a report; false for the others.
    bool report(QcServerClient &client, const QcMsg::AdminRequest &request);
    // The command line a request stands for; empty when its arguments do not fit.
    [[nodiscard]] static QString commandLine(const QcMsg::AdminRequest &request);
    // Runs `line` as the client's command (or with `console` as the console's) and answers `id`
    // with the replies.
    void run(QcServerClient &client, quint32 id, const QString &line, bool console);
    // The reports a change of this kind may have changed, sent after it.
    void sendChanged(QcServerClient &client, QcMsg::AdminRequest::Kind kind);
    void pong(const QcServerClient &client, quint32 nonce);
    // Ends subscriptions of clients who left or lost their rights.
    void dropUnauthorized();
    void sendChat();
    // The sample of the second that ended; the status, players and log to those who follow them.
    void sampleSecond();
    void sendLog();
    void sendPings();
    QHash<quint32, qint32> pings() const;

    QcServerState &m_state;
    QcServerCommands &m_commands;
    QcAdminReports m_reports;
    QcAdminStatusRecorder m_recorder;
    QHash<quint32, Subscriber> m_subscribers;
    QHash<quint32, Ping> m_pings;
    // Players' time of each client's latest log tail.
    QHash<quint32, qint64> m_lastTail;
    QcTraffic m_departed;
    QElapsedTimer m_uptime;
    qint64 m_lastSampleNs = 0;
    qint64 m_lastPingNs = -pingIntervalNs;
};
