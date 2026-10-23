#include "clientcore/admin/QcClientAdmin.h"

#include "clientcore/debug/QcClientLogging.h"
#include "common/protocol/QcAdminSamples.h"
#include "common/protocol/QcChatText.h"

namespace {

// `list` with `added` appended, at most `limit` of the newest.
template<typename T>
[[nodiscard]] QList<T> appendedBounded(QList<T> list, const QList<T> &added, qsizetype limit)
{
    list.append(added);
    if (list.size() > limit) {
        list.remove(0, list.size() - limit);
    }
    return list;
}

// The reason of a refused answer; a failure without a reason counts as wrong arguments.
QcClientAdmin::Refusal refusalOf(const QcMsg::AdminResult &result)
{
    using Wire = QcMsg::AdminResult::Refusal;
    static_assert(static_cast<int>(Wire::TooSoon) == static_cast<int>(QcClientAdmin::Refusal::TooSoon), "the client's reasons follow the protocol's");
    if (result.ok) {
        return QcClientAdmin::Refusal::None;
    }
    return result.refusal == Wire::None ? QcClientAdmin::Refusal::Arguments : static_cast<QcClientAdmin::Refusal>(result.refusal);
}

} // namespace

QcClientAdmin::QcClientAdmin(QObject *parent)
    : QObject(parent)
{
}

void QcClientAdmin::welcomed()
{
    m_welcomed = true;
    if (m_topics != 0) {
        sendFollow();
    }
}

void QcClientAdmin::follow(quint8 topics, QcMsg::AdminLogLine::Level logLevel)
{
    const quint8 wanted = topics & QcMsg::AdminSubscribe::all;
    if (wanted != topics) {
        qCWarning(lcSession) << "Admin topics" << topics << "include unknown ones; following" << wanted;
    }
    if (wanted == m_topics && logLevel == m_logLevel) {
        // The same subscription: the server already sends it.
        return;
    }
    // A topic followed anew starts from the server's history, not from what came before.
    if ((wanted & ~m_topics & QcMsg::AdminSubscribe::chat) != 0) {
        m_chat.clear();
        ++m_chatArrivals.replacements;
    }
    if ((wanted & ~m_topics & QcMsg::AdminSubscribe::log) != 0 || logLevel != m_logLevel) {
        m_log.clear();
        ++m_logArrivals.replacements;
    }
    m_topics = wanted;
    m_logLevel = logLevel;
    if (m_welcomed) {
        sendFollow();
    }
}

void QcClientAdmin::closed()
{
    m_welcomed = false;
    setOperatorRights(false);
}

void QcClientAdmin::setOperatorRights(bool rights)
{
    if (rights != m_operatorRights) {
        m_operatorRights = rights;
        emit operatorRightsChanged();
    }
}

void QcClientAdmin::sendFollow()
{
    if (m_send) {
        m_send(QcMsg::AdminSubscribe{m_topics, static_cast<quint8>(m_logLevel)});
    }
}

quint32 QcClientAdmin::request(Kind kind, const QStringList &arguments)
{
    if (!m_welcomed || !m_send) {
        qCWarning(lcSession) << "Admin request" << static_cast<int>(kind) << "not sent: no server has welcomed the session";
        return 0;
    }
    const quint32 id = m_nextId++;
    m_send(QcMsg::AdminRequest{id, kind, arguments});
    return id;
}

std::optional<QcMsg::AdminResult> QcClientAdmin::result(quint32 id) const
{
    const auto found = m_results.constFind(id);
    return found == m_results.cend() ? std::nullopt : std::optional(*found);
}

QStringList QcClientAdmin::display(const QcMsg::AdminResult &result)
{
    QStringList lines;
    for (const QcMsg::ChatMessage &line : result.lines) {
        lines.append(QcChatText::display(line));
    }
    return lines;
}

bool QcClientAdmin::receive(const QcMessage &message)
{
    if (const auto *answer = std::get_if<QcMsg::AdminResult>(&message)) {
        receiveResult(*answer);
        return true;
    }
    return receiveReport(message);
}

void QcClientAdmin::receiveResult(const QcMsg::AdminResult &result)
{
    m_lastRefusal = refusalOf(result);
    // Without rights the server follows nothing for this client any more; a new follow() asks again.
    if (m_lastRefusal == Refusal::Rights) {
        m_topics = 0;
        setOperatorRights(false);
    } else if (result.ok) {
        setOperatorRights(true);
    }
    if (!result.ok) {
        emit refused(m_lastRefusal, display(result).join(u'\n'));
    }
    if (result.id != 0) {
        m_results.insert(result.id, result);
        m_resultOrder.append(result.id);
        if (m_resultOrder.size() > resultLimit) {
            m_results.remove(m_resultOrder.takeFirst());
        }
        emit resultReceived(result.id);
    }
}

bool QcClientAdmin::receiveReport(const QcMessage &message)
{
    if (const auto *status = std::get_if<QcMsg::AdminStatus>(&message)) {
        m_status = *status;
        // The status right after the history repeats its newest sample.
        if (m_history.seconds.isEmpty() || m_history.seconds.last() != status->sample) {
            m_history.seconds = appendedBounded(std::move(m_history.seconds), {status->sample}, QcAdminSamples::secondsKept);
            // The day goes on as on the server: every 60 seconds make a minute.
            m_history = QcAdminSamples::withMinuteFolded(std::move(m_history));
        }
        emit statusChanged();
    } else if (const auto *history = std::get_if<QcMsg::AdminHistory>(&message)) {
        m_history = *history;
        emit historyChanged();
    } else if (const auto *players = std::get_if<QcMsg::AdminPlayers>(&message)) {
        m_players = players->players;
        emit playersChanged();
    } else if (const auto *chat = std::get_if<QcMsg::AdminChat>(&message)) {
        if (chat->history) {
            m_chat.clear();
            ++m_chatArrivals.replacements;
        }
        m_chat = appendedBounded(std::move(m_chat), chat->lines, chatLimit);
        m_chatArrivals.total += static_cast<quint64>(chat->lines.size());
        emit chatReceived();
    } else if (const auto *catalogue = std::get_if<QcMsg::AdminCatalogue>(&message)) {
        m_catalogue = *catalogue;
        emit catalogueChanged();
    } else if (const auto *lists = std::get_if<QcMsg::AdminLists>(&message)) {
        m_lists = *lists;
        emit listsChanged();
    } else if (const auto *settings = std::get_if<QcMsg::AdminSettings>(&message)) {
        m_settings = *settings;
        emit settingsChanged();
    } else if (const auto *log = std::get_if<QcMsg::AdminLog>(&message)) {
        if (log->tail) {
            m_log.clear();
            ++m_logArrivals.replacements;
        }
        m_log = appendedBounded(std::move(m_log), log->lines, logLimit);
        m_logArrivals.total += static_cast<quint64>(log->lines.size());
        emit logReceived();
    } else {
        return false;
    }
    return true;
}
