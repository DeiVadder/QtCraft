#include "app/admin/QcAdminController.h"

#include "app/admin/QcAdminViews.h"
#include "app/debug/QcAppLogging.h"
#include "clientcore/admin/QcAdminCompletion.h"
#include "clientcore/session/QcClientSession.h"
#include "common/auth/QcProfileValidation.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcAdminSamples.h"

#include <QDateTime>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

using Kind = QcClientAdmin::Kind;
using Level = QcMsg::AdminLogLine::Level;

// Completions the command page offers at once.
constexpr qsizetype suggestionLimit = 8;

// How a list model catches up with the channel's lines.
struct CatchUp
{
    // A new history or tail: all lines anew.
    bool replace = false;
    // Else the newest this many lines.
    qsizetype added = 0;
};

// What a model that saw `seen` of a list needs for its `available` lines now arrived `now`.
CatchUp catchUp(QcClientAdmin::Arrivals seen, QcClientAdmin::Arrivals now, qsizetype available)
{
    if (now.replacements != seen.replacements || now.total < seen.total) {
        return {true, 0};
    }
    return {false, static_cast<qsizetype>(std::min<quint64>(now.total - seen.total, static_cast<quint64>(available)))};
}

// The newest point of the series the charts show and the number of points.
std::pair<qint64, qsizetype> newestPoint(const QList<QcMsg::AdminSample> &series)
{
    return {series.isEmpty() ? 0 : series.last().timeMs, series.size()};
}

} // namespace

QcAdminController::QcAdminController(QObject *parent)
    : QObject(parent)
{
}

void QcAdminController::sessionStarted(QcClientSession *session)
{
    m_ownName = session ? session->playerName() : QString();
    if (!session) {
        follow(nullptr, {});
        return;
    }
    follow(&session->admin(), [session] { return session->content(); });
}

bool QcAdminController::isOwnName(const QString &name) const
{
    // Without a session there is no own name (playerName() would make "Player" of it).
    if (m_ownName.isEmpty()) {
        return false;
    }
    return QcProfileValidation::playerName(name).toCaseFolded() == QcProfileValidation::playerName(m_ownName).toCaseFolded();
}

void QcAdminController::follow(QcClientAdmin *admin, ContentSource content)
{
    close();
    if (m_admin) {
        disconnect(m_admin, nullptr, this, nullptr);
    }
    m_admin = admin;
    m_content = std::move(content);
    m_pending.clear();
    m_output.clear();
    // A draft belongs to the server it was made for.
    m_draft.clear();
    m_settingsSaved = 0;
    m_settingRefusals.clear();
    emit draftChanged();
    m_chat.replace(m_admin ? QcAdminViews::chatLines(m_admin->chat()) : QList<QcAdminLineModel::Line>());
    m_log.replace(m_admin ? QcAdminViews::logLines(m_admin->log()) : QList<QcAdminLineModel::Line>());
    m_chatSeen = m_admin ? m_admin->chatArrivals() : QcClientAdmin::Arrivals();
    m_logSeen = m_admin ? m_admin->logArrivals() : QcClientAdmin::Arrivals();
    setNotice(Notice::None, {});
    if (m_admin) {
        connectAdmin();
    }
    emit operatorRightsChanged();
}

void QcAdminController::connectAdmin()
{
    const QcClientAdmin *admin = m_admin.data();
    connect(admin, &QcClientAdmin::operatorRightsChanged, this, &QcAdminController::onRightsChanged);
    // The session (and its channel) went: no operator any more.
    connect(admin, &QObject::destroyed, this, [this] {
        m_active = false;
        emit activeChanged();
        emit operatorRightsChanged();
    });
    connect(admin, &QcClientAdmin::statusChanged, this, &QcAdminController::updateStatus);
    connect(admin, &QcClientAdmin::historyChanged, this, [this] { updateCharts(true); });
    connect(admin, &QcClientAdmin::playersChanged, this, &QcAdminController::updatePlayers);
    connect(admin, &QcClientAdmin::chatReceived, this, &QcAdminController::showChat);
    connect(admin, &QcClientAdmin::logReceived, this, &QcAdminController::showLog);
    connect(admin, &QcClientAdmin::catalogueChanged, this, [this] {
        m_commands.setCatalogue(m_admin->catalogue());
        ++m_catalogueRevision;
        emit catalogueChanged();
    });
    connect(admin, &QcClientAdmin::listsChanged, this, &QcAdminController::updateLists);
    connect(admin, &QcClientAdmin::settingsChanged, this, &QcAdminController::updateSettings);
    connect(admin, &QcClientAdmin::resultReceived, this, &QcAdminController::onResult);
    connect(admin, &QcClientAdmin::refused, this, &QcAdminController::onRefused);
}

void QcAdminController::open()
{
    if (!m_admin) {
        qCWarning(lcGame) << "Manage server: no session to manage";
        return;
    }
    if (m_active) {
        // Opened again while open (the overlay changed between its pages): it follows already.
        return;
    }

    m_active = true;
    emit activeChanged();
    setNotice(Notice::None, {});
    subscribe();
}

void QcAdminController::subscribe()
{
    m_admin->follow(QcMsg::AdminSubscribe::all, static_cast<Level>(m_logLevel));
    // Following anew starts from the server's history and tail.
    m_chat.replace(QcAdminViews::chatLines(m_admin->chat()));
    m_log.replace(QcAdminViews::logLines(m_admin->log()));
    m_chatSeen = m_admin->chatArrivals();
    m_logSeen = m_admin->logArrivals();
    for (const Kind kind : {Kind::Catalogue, Kind::Lists, Kind::Settings}) {
        m_admin->request(kind);
    }
    // What came before (a session that had the screen open) shows at once.
    updateStatus();
    updateCharts(true);
    updatePlayers();
    updateLists();
    updateSettings();
}

void QcAdminController::close()
{
    if (!m_active) {
        return;
    }
    m_active = false;
    emit activeChanged();
    if (m_admin) {
        m_admin->follow(0, static_cast<Level>(m_logLevel));
    }
}

void QcAdminController::onRightsChanged()
{
    emit operatorRightsChanged();
    // Rights given back while the screen is open (the refusal ended following): it follows again.
    if (m_active && m_admin && m_admin->operatorRights() && m_admin->topics() == 0) {
        subscribe();
        if (m_noticeKind == Notice::Refused) {
            setNotice(Notice::None, {});
        }
    }
}

void QcAdminController::showChat()
{
    const CatchUp update = catchUp(m_chatSeen, m_admin->chatArrivals(), m_admin->chat().size());
    m_chatSeen = m_admin->chatArrivals();
    if (update.replace) {
        m_chat.replace(QcAdminViews::chatLines(m_admin->chat()));
    } else {
        m_chat.append(QcAdminViews::chatLines(m_admin->chat().last(update.added)), QcClientAdmin::chatLimit);
    }
}

void QcAdminController::showLog()
{
    const CatchUp update = catchUp(m_logSeen, m_admin->logArrivals(), m_admin->log().size());
    m_logSeen = m_admin->logArrivals();
    if (update.replace) {
        m_log.replace(QcAdminViews::logLines(m_admin->log()));
    } else {
        m_log.append(QcAdminViews::logLines(m_admin->log().last(update.added)), QcClientAdmin::logLimit);
    }
}

void QcAdminController::setMinutes(bool minutes)
{
    if (minutes == m_minutes) {
        return;
    }
    m_minutes = minutes;
    updateCharts(true);
}

void QcAdminController::setChartsShown(bool shown)
{
    if (shown == m_chartsShown) {
        return;
    }
    m_chartsShown = shown;
    emit chartsShownChanged();
    updateCharts(true);
}

int QcAdminController::chartCapacity() const
{
    return static_cast<int>(m_minutes ? QcAdminSamples::minutesKept : QcAdminSamples::secondsKept);
}

int QcAdminController::ticksPerSecond()
{
    return static_cast<int>(QcPhysics::tickRate);
}

int QcAdminController::tickBudgetMs()
{
    return static_cast<int>(1000.0 / QcPhysics::tickRate);
}

int QcAdminController::nameLength()
{
    return static_cast<int>(QcProfileValidation::maxPlayerNameLength);
}

int QcAdminController::textLength()
{
    return static_cast<int>(QcMsg::AdminLimits::maxText);
}

int QcAdminController::reasonLength()
{
    return static_cast<int>(QcMsg::AdminLimits::maxReason);
}

int QcAdminController::labelLength()
{
    return static_cast<int>(QcMsg::AdminLimits::maxBackupLabel);
}

void QcAdminController::setLogLevel(LogLevel level)
{
    if (static_cast<int>(level) < static_cast<int>(LogLevel::Debug) || static_cast<int>(level) > static_cast<int>(LogLevel::Critical)) {
        qCWarning(lcGame) << "Manage server: no log level" << static_cast<int>(level);
        return;
    }
    if (level == m_logLevel) {
        return;
    }

    m_logLevel = level;
    emit logLevelChanged();
    if (m_active && m_admin) {
        // The server answers a new level with its tail at that level (not limited like LogTail),
        // which replaces the lines shown when it arrives; new lines follow from its newest on.
        m_admin->follow(QcMsg::AdminSubscribe::all, static_cast<Level>(level));
    }
}

bool QcAdminController::limitsFromServer() const
{
    return m_admin && m_admin->settings().limitsFromServer;
}

void QcAdminController::act(QcClientAdmin::Kind kind, const QStringList &arguments)
{
    if (!m_admin) {
        qCWarning(lcGame) << "Manage server: no session for request" << static_cast<int>(kind);
        return;
    }
    if (const quint32 id = m_admin->request(kind, arguments); id != 0) {
        m_pending.insert(id, {});
    }
}

// Names may not have spaces (the server takes the words of a command line); a reason may.
void QcAdminController::kick(const QString &name, const QString &reason)
{
    act(Kind::Kick, reason.trimmed().isEmpty() ? QStringList{name.trimmed()} : QStringList{name.trimmed(), reason.trimmed()});
}

void QcAdminController::ban(const QString &name, const QString &reason)
{
    act(Kind::Ban, reason.trimmed().isEmpty() ? QStringList{name.trimmed()} : QStringList{name.trimmed(), reason.trimmed()});
}

void QcAdminController::unban(const QString &name)
{
    act(Kind::Unban, {name.trimmed()});
}

void QcAdminController::op(const QString &name)
{
    act(Kind::Op, {name.trimmed()});
}

void QcAdminController::deop(const QString &name)
{
    act(Kind::Deop, {name.trimmed()});
}

void QcAdminController::whitelistAdd(const QString &name)
{
    act(Kind::WhitelistAdd, {name.trimmed()});
}

void QcAdminController::whitelistRemove(const QString &name)
{
    act(Kind::WhitelistRemove, {name.trimmed()});
}

void QcAdminController::setWhitelist(bool enabled)
{
    act(Kind::Whitelist, {enabled ? u"on"_s : u"off"_s});
}

void QcAdminController::changeSetting(bool limit, const QString &key, qint64 value)
{
    const QcMsg::AdminSettings::Setting *setting = m_admin ? QcAdminSettingsDraft::find(m_admin->settings(), limit, key) : nullptr;
    if (!setting) {
        qCWarning(lcGame) << "Manage server: no setting" << key << "to change";
        return;
    }
    if (!QcAdminSettingsDraft::parsed(QString::number(value), *setting)) {
        qCWarning(lcGame) << "Manage server:" << value << "is out of the range of" << key;
        return;
    }
    m_draft.change(*setting, limit, value);
    emit draftChanged();
}

bool QcAdminController::changeSettingText(bool limit, const QString &key, const QString &text)
{
    const QcMsg::AdminSettings::Setting *setting = m_admin ? QcAdminSettingsDraft::find(m_admin->settings(), limit, key) : nullptr;
    const std::optional<qint64> value = setting ? QcAdminSettingsDraft::parsed(text, *setting) : std::nullopt;
    // Typing passes through values that are none (a lone minus): the field shows it, no warning.
    if (!value) {
        return false;
    }
    m_draft.change(*setting, limit, *value);
    emit draftChanged();
    return true;
}

void QcAdminController::saveSettings()
{
    if (!m_admin) {
        qCWarning(lcGame) << "Manage server: no session to save the settings to";
        return;
    }
    m_settingsSaved = 0;
    m_settingRefusals.clear();
    const QList<QcAdminSettingsDraft::Entry> entries = m_draft.entries();
    for (const QcAdminSettingsDraft::Entry &entry : entries) {
        const quint32 id = m_admin->request(entry.limit ? Kind::SetLimit : Kind::SetRule, {entry.key, QString::number(entry.value)});
        if (id != 0) {
            m_pending.insert(id, {false, QcAdminSettingsDraft::id(entry.limit, entry.key), entry.value});
        }
    }
}

void QcAdminController::discardSettings()
{
    m_draft.clear();
    emit draftChanged();
}

void QcAdminController::save()
{
    act(Kind::Save, {});
}

void QcAdminController::backup(const QString &label)
{
    act(Kind::Backup, label.trimmed().isEmpty() ? QStringList{} : QStringList{label.trimmed()});
}

void QcAdminController::stop()
{
    // The screen asked first (a modal dialog): the server needs the confirmation word.
    act(Kind::Stop, {u"confirm"_s});
}

void QcAdminController::sendChat(const QString &text)
{
    if (!text.trimmed().isEmpty()) {
        act(Kind::Chat, {text.trimmed()});
    }
}

void QcAdminController::runCommand(const QString &line)
{
    const QString command = line.trimmed();
    if (command.isEmpty() || !m_admin) {
        return;
    }
    appendOutput({u"> "_s + command}, static_cast<int>(Level::Info));
    if (const quint32 id = m_admin->runCommand(command); id != 0) {
        m_pending.insert(id, {true, {}, 0});
    }
}

void QcAdminController::clearOutput()
{
    m_output.clear();
}

void QcAdminController::clearNotice()
{
    setNotice(Notice::None, {});
}

QStringList QcAdminController::complete(const QString &line) const
{
    if (!m_admin) {
        return {};
    }
    QStringList names;
    for (const QcMsg::AdminPlayers::Entry &player : m_admin->players()) {
        names.append(player.name);
    }
    const std::shared_ptr<const QcContentRegistry> content = m_content ? m_content() : nullptr;
    return QcAdminCompletion::complete(line, {m_admin->catalogue(), names, content.get()}, suggestionLimit);
}

QVariantMap QcAdminController::help(const QString &line) const
{
    return m_admin ? QcAdminViews::help(line, m_admin->catalogue()) : QVariantMap();
}

QString QcAdminController::refusalText(QcClientAdmin::Refusal reason, const QString &text)
{
    switch (reason) {
    case QcClientAdmin::Refusal::Rights:
        return tr("You are no operator on this server (any more). Managing it has ended.");
    case QcClientAdmin::Refusal::Confirmation:
        return tr("The server needs a confirmation for this.");
    case QcClientAdmin::Refusal::TooSoon:
        return tr("Too many requests at once. Try again in a moment.");
    case QcClientAdmin::Refusal::Arguments:
    case QcClientAdmin::Refusal::None:
        break;
    }
    return text.isEmpty() ? tr("The server did not accept this.") : tr("The server did not accept this: %1").arg(text);
}

void QcAdminController::onResult(quint32 id)
{
    if (!m_pending.contains(id) || !m_admin) {
        // Asked by someone else (the control interface) or a report the screen asked for.
        return;
    }
    const Pending asked = m_pending.take(id);

    const std::optional<QcMsg::AdminResult> result = m_admin->result(id);
    if (!result) {
        return;
    }
    if (!asked.setting.isEmpty()) {
        onSettingSaved(asked, *result);
        return;
    }
    // A command's own failure (no reason) says itself what went wrong.
    const QStringList lines = QcClientAdmin::display(*result);
    const bool reasoned = result->refusal != QcMsg::AdminResult::Refusal::None;
    if (asked.command) {
        if (reasoned) {
            appendOutput({refusalText(m_admin->lastRefusal(), lines.join(u' '))}, static_cast<int>(Level::Critical));
        } else {
            appendOutput(lines, static_cast<int>(result->ok ? Level::Info : Level::Warning));
        }
    } else if (!result->ok) {
        setNotice(Notice::Refused, refusalText(m_admin->lastRefusal(), lines.join(u' ')));
    } else if (!lines.isEmpty()) {
        setNotice(Notice::Done, lines.join(u'\n'));
    }
}

void QcAdminController::onSettingSaved(const Pending &pending, const QcMsg::AdminResult &result)
{
    ++m_settingsSaved;
    if (result.ok) {
        m_draft.saved(pending.setting, pending.value);
        emit draftChanged();
    } else {
        // The value stays in the draft; the notice names it with the server's reason.
        const QString key = pending.setting.mid(pending.setting.indexOf(u':') + 1);
        m_settingRefusals.append(tr("%1: %2").arg(key, refusalText(m_admin->lastRefusal(), QcClientAdmin::display(result).join(u' '))));
    }

    const bool more = std::ranges::any_of(m_pending, [](const Pending &other) { return !other.setting.isEmpty(); });
    if (more) {
        return;
    }
    const int refused = static_cast<int>(m_settingRefusals.size());
    if (refused == 0) {
        setNotice(Notice::Done, tr("Saved %n value(s).", "", m_settingsSaved));
    } else {
        setNotice(Notice::Refused, tr("%n value(s) not saved; they stay changed here:", "", refused) + u'\n' + m_settingRefusals.join(u'\n'));
    }
    m_settingsSaved = 0;
    m_settingRefusals.clear();
}

void QcAdminController::onRefused(QcClientAdmin::Refusal reason, const QString &text)
{
    // Answers to the screen's requests show by onResult; missing rights end everything, whatever
    // was asked (the subscription too), so they always show.
    Q_UNUSED(text)
    if (m_active && reason == QcClientAdmin::Refusal::Rights) {
        setNotice(Notice::Refused, refusalText(reason, {}));
    }
}

void QcAdminController::setNotice(Notice kind, const QString &text)
{
    if (kind == m_noticeKind && text == m_notice) {
        return;
    }
    m_noticeKind = kind;
    m_notice = text;
    emit noticeChanged();
}

void QcAdminController::appendOutput(const QStringList &lines, int level)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QList<QcAdminLineModel::Line> added;
    for (const QString &line : lines) {
        added.append({now, level, {}, line});
    }
    m_output.append(added, outputLimit);
}

void QcAdminController::updateStatus()
{
    m_status = m_admin && m_admin->status() ? QcAdminViews::status(*m_admin->status()) : QVariantMap();
    emit statusChanged();
    // Each status adds its sample to the history (and every 60th a minute).
    updateCharts();
}

void QcAdminController::updateCharts(bool force)
{
    if (!m_chartsShown || !m_admin) {
        // Built anew when shown again; the span may have changed the capacity.
        m_chartsBuilt = {-1, 0};
        if (force) {
            emit chartsChanged();
        }
        return;
    }
    const auto built = newestPoint(m_minutes ? m_admin->history().minutes : m_admin->history().seconds);
    if (!force && built == m_chartsBuilt) {
        return;
    }
    m_chartsBuilt = built;
    m_charts = QcAdminViews::charts(m_admin->history(), m_minutes);
    emit chartsChanged();
}

void QcAdminController::updatePlayers()
{
    m_players = m_admin ? QcAdminViews::players(m_admin->players()) : QVariantList();
    emit playersChanged();
}

void QcAdminController::updateLists()
{
    m_lists = m_admin ? QcAdminViews::lists(m_admin->lists()) : QVariantMap();
    emit listsChanged();
}

void QcAdminController::updateSettings()
{
    if (m_admin && !m_draft.isEmpty()) {
        m_draft.prune(m_admin->settings());
        emit draftChanged();
    }
    m_ruleGroups = m_admin ? QcAdminViews::groups(m_admin->settings().rules, false) : QVariantList();
    m_limitGroups = m_admin ? QcAdminViews::groups(m_admin->settings().limits, true) : QVariantList();
    emit settingsChanged();
}
