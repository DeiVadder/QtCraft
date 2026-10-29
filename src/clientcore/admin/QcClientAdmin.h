// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/protocol/QcMessages.h"

#include <QHash>
#include <QList>
#include <QObject>

#include <functional>
#include <optional>

// The client's side of the admin channel (decision E-848), part of QcClientSession: what an
// operator asks the server for and what came back, for the "Manage server" screen (stage 77.2) and
// the control interface. The topics followed (status, chat, log) are sent once the server welcomed
// the client and again with every change; requests need the welcome and get a number, which the
// answer (QcMsg::AdminResult) carries. Every report the server sends replaces the one before; the
// status history grows with each status by its sample, chat and log lines are kept up to their
// limits. Only operators get answers: a refusal arrives as `refused` and as a failed result.
class QcClientAdmin : public QObject
{
    Q_OBJECT

public:
    using Sender = std::function<void(const QcMessage &)>;
    using Kind = QcMsg::AdminRequest::Kind;
    // Why the server refused (QcMsg::AdminResult::Refusal), for the screen to say the right thing:
    // missing rights end every subscription.
    enum class Refusal { None, Rights, Arguments, Confirmation, TooSoon };
    Q_ENUM(Refusal)
    static constexpr qsizetype chatLimit = 500;
    static constexpr qsizetype logLimit = 2000;
    static constexpr qsizetype resultLimit = 64;

    explicit QcClientAdmin(QObject *parent = nullptr);

    void setSender(Sender sender) { m_send = std::move(sender); }
    // The server welcomed the client: the topics followed go out now.
    void welcomed();
    // The connection closed: nothing goes out any more, what came stays.
    void closed();
    // Takes an admin message of the server; false for every other message.
    bool receive(const QcMessage &message);

    // Follows the topics (QcMsg::AdminSubscribe flags, 0 for none) with log lines from `logLevel` on.
    void follow(quint8 topics, QcMsg::AdminLogLine::Level logLevel = QcMsg::AdminLogLine::Level::Info);
    quint8 topics() const { return m_topics; }
    // Sends a request; its number, or 0 when it cannot go out (not welcomed, connection closed).
    quint32 request(Kind kind, const QStringList &arguments = {});
    // A command line as the operator typed it, with or without '/'.
    quint32 runCommand(const QString &line) { return request(Kind::Command, {line}); }

    const std::optional<QcMsg::AdminStatus> &status() const { return m_status; }
    const QcMsg::AdminHistory &history() const { return m_history; }
    const QList<QcMsg::AdminPlayers::Entry> &players() const { return m_players; }
    const QList<QcMsg::AdminChatLine> &chat() const { return m_chat; }
    const QcMsg::AdminCatalogue &catalogue() const { return m_catalogue; }
    const QcMsg::AdminLists &lists() const { return m_lists; }
    const QcMsg::AdminSettings &settings() const { return m_settings; }
    const QList<QcMsg::AdminLogLine> &log() const { return m_log; }
    // How many chat and log lines came so far (on over their limits), and how often the list was
    // replaced (a new history or tail, following anew): a view appends only what is new (decision
    // E-863).
    struct Arrivals
    {
        quint64 total = 0;
        quint64 replacements = 0;

        bool operator==(const Arrivals &) const = default;
    };
    Arrivals chatArrivals() const { return m_chatArrivals; }
    Arrivals logArrivals() const { return m_logArrivals; }
    // The answer to request `id` while it is among the latest resultLimit; std::nullopt before it came.
    std::optional<QcMsg::AdminResult> result(quint32 id) const;
    // The answer's lines as the player reads them (translated, QcChatText::display).
    static QStringList display(const QcMsg::AdminResult &result);
    // Why the latest answer of the server was refused; None when it was not.
    Refusal lastRefusal() const { return m_lastRefusal; }
    // The server said this player is an operator (decision E-863): it tells every player when he
    // becomes one and when he stops being one; every answer that was not refused says so too.
    bool operatorRights() const { return m_operatorRights; }

signals:
    void statusChanged();
    void historyChanged();
    void playersChanged();
    void chatReceived();
    void catalogueChanged();
    void listsChanged();
    void settingsChanged();
    void logReceived();
    void resultReceived(quint32 id);
    void operatorRightsChanged();
    // The server refused a request or the subscription; `text` is translated. With Rights the
    // player is no operator (any more) and follows nothing now.
    void refused(QcClientAdmin::Refusal reason, const QString &text);

private:
    bool receiveReport(const QcMessage &message);
    void receiveResult(const QcMsg::AdminResult &result);
    void sendFollow();
    void setOperatorRights(bool rights);

    Sender m_send;
    bool m_welcomed = false;
    quint8 m_topics = 0;
    QcMsg::AdminLogLine::Level m_logLevel = QcMsg::AdminLogLine::Level::Info;
    quint32 m_nextId = 1;
    std::optional<QcMsg::AdminStatus> m_status;
    QcMsg::AdminHistory m_history;
    QList<QcMsg::AdminPlayers::Entry> m_players;
    QList<QcMsg::AdminChatLine> m_chat;
    QcMsg::AdminCatalogue m_catalogue;
    QcMsg::AdminLists m_lists;
    QcMsg::AdminSettings m_settings;
    QList<QcMsg::AdminLogLine> m_log;
    Arrivals m_chatArrivals;
    Arrivals m_logArrivals;
    QHash<quint32, QcMsg::AdminResult> m_results;
    QList<quint32> m_resultOrder;
    Refusal m_lastRefusal = Refusal::None;
    bool m_operatorRights = false;
};
