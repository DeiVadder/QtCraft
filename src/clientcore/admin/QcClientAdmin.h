// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/protocol/QcMessages.h"

#include <QHash>
#include <QList>
#include <QObject>

#include <functional>
#include <optional>

/*! \brief The client's side of the admin channel (decision E-848), part of QcClientSession: what an
    operator asks the server for and what came back, for the "Manage server" screen (stage 77.2) and
    the control interface.

    The topics followed (status, chat, log) are sent once the server welcomed the client and
    again with every change; requests need the welcome and get a number, which the answer
    (QcMsg::AdminResult) carries. Every report the server sends replaces the one before; the
    status history grows with each status by its sample, chat and log lines are kept up to their
    limits. Only operators get answers: a refusal arrives as \c {refused} and as a failed result. */
class QcClientAdmin : public QObject
{
    Q_OBJECT

public:
    /*! Sends a message to the server. */
    using Sender = std::function<void(const QcMessage &)>;
    /*! What a request asks for. */
    using Kind = QcMsg::AdminRequest::Kind;
    /*! \brief Why the server refused (QcMsg::AdminResult::Refusal), for the screen to say the
        right thing: missing rights end every subscription. */
    enum class Refusal {
        None,         // not refused
        Rights,       // the player is no operator
        Arguments,    // the arguments do not fit the request
        Confirmation, // stopping needs the argument "confirm"
        TooSoon       // the request came again too soon
    };
    Q_ENUM(Refusal)
    /*! Chat lines kept at most; the oldest go first. */
    static constexpr qsizetype chatLimit = 500;
    /*! Log lines kept at most; the oldest go first. */
    static constexpr qsizetype logLimit = 2000;
    /*! Answers kept at most (result()). */
    static constexpr qsizetype resultLimit = 64;

    explicit QcClientAdmin(QObject *parent = nullptr);

    /*! Sets the way out to the server to \a sender (the session's connection). */
    void setSender(Sender sender) { m_send = std::move(sender); }
    /*! The server welcomed the client: the topics followed go out now. */
    void welcomed();
    /*! The connection closed: nothing goes out any more, what came stays. */
    void closed();
    /*! Takes the admin \a message of the server. Returns false for every other message. */
    bool receive(const QcMessage &message);

    /*! Follows the \a topics (QcMsg::AdminSubscribe flags, 0 for none) with log lines from
        \a logLevel on. */
    void follow(quint8 topics, QcMsg::AdminLogLine::Level logLevel = QcMsg::AdminLogLine::Level::Info);
    /*! Returns the topics followed (QcMsg::AdminSubscribe flags). */
    quint8 topics() const { return m_topics; }
    /*! Sends a request of \a kind with \a arguments. Returns its number, or 0 when it cannot go
        out (not welcomed, connection closed). */
    quint32 request(Kind kind, const QStringList &arguments = {});
    /*! Sends the command \a line as the operator typed it, with or without '/'. Returns the
        request's number, or 0 when it cannot go out. */
    quint32 runCommand(const QString &line) { return request(Kind::Command, {line}); }

    /*! Returns the server's latest status report; nothing before the first. */
    const std::optional<QcMsg::AdminStatus> &status() const { return m_status; }
    /*! Returns the status history: one sample per status report. */
    const QcMsg::AdminHistory &history() const { return m_history; }
    /*! Returns the players as the server last reported them. */
    const QList<QcMsg::AdminPlayers::Entry> &players() const { return m_players; }
    /*! Returns the chat lines that came, oldest first, at most chatLimit. */
    const QList<QcMsg::AdminChatLine> &chat() const { return m_chat; }
    /*! Returns the commands the server offers with their usage. */
    const QcMsg::AdminCatalogue &catalogue() const { return m_catalogue; }
    /*! Returns the operator, ban and whitelist lists as the server last reported them. */
    const QcMsg::AdminLists &lists() const { return m_lists; }
    /*! Returns the game rules and server limits as last reported. */
    const QcMsg::AdminSettings &settings() const { return m_settings; }
    /*! Returns the log lines that came, oldest first, at most logLimit. */
    const QList<QcMsg::AdminLogLine> &log() const { return m_log; }
    /*! \brief How many chat and log lines came so far (on over their limits), and how often the
        list was replaced (a new history or tail, following anew): a view appends only what is
        new (decision E-863). */
    struct Arrivals
    {
        /*! Lines that came so far. */
        quint64 total = 0;
        /*! How often the list was replaced. */
        quint64 replacements = 0;

        bool operator==(const Arrivals &) const = default;
    };
    /*! Returns how many chat lines came and how often the list was replaced. */
    Arrivals chatArrivals() const { return m_chatArrivals; }
    /*! Returns how many log lines came and how often the list was replaced. */
    Arrivals logArrivals() const { return m_logArrivals; }
    /*! Returns the answer to request \a id while it is among the latest resultLimit;
        std::nullopt before it came. */
    std::optional<QcMsg::AdminResult> result(quint32 id) const;
    /*! Returns the lines of the answer \a result as the player reads them (translated,
        QcChatText::display). */
    static QStringList display(const QcMsg::AdminResult &result);
    /*! Returns why the latest answer of the server was refused; None when it was not. */
    Refusal lastRefusal() const { return m_lastRefusal; }
    /*! Returns whether the server said this player is an operator (decision E-863): it tells every
        player when they become one and when they stop being one; every answer that was not refused
        says so too. */
    bool operatorRights() const { return m_operatorRights; }

signals:
    /*! A new status report came. */
    void statusChanged();
    /*! The status history changed. */
    void historyChanged();
    /*! A new list of players came. */
    void playersChanged();
    /*! Chat lines came (chat(), chatArrivals()). */
    void chatReceived();
    /*! The catalogue of commands came. */
    void catalogueChanged();
    /*! The lists of operators, bans and the whitelist came. */
    void listsChanged();
    /*! The game rules and server limits came. */
    void settingsChanged();
    /*! Log lines came (log(), logArrivals()). */
    void logReceived();
    /*! The answer to request \a id came (result()). */
    void resultReceived(quint32 id);
    /*! operatorRights() changed. */
    void operatorRightsChanged();
    /*! Emitted when the server refused a request or the subscription for \a reason; \a text is
        translated. With Rights the player is no operator (any more) and follows nothing now. */
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
