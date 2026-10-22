#pragma once

#include "app/admin/QcAdminLineModel.h"
#include "app/admin/QcAdminSettingsDraft.h"
#include "clientcore/admin/QcClientAdmin.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>
#include <memory>
#include <utility>

class QcClientSession;
class QcContentRegistry;

// The "Manage server" screen for QML (GameController.admin, decision E-863): an operator's view of
// the server over the admin channel of his session (QcClientAdmin, decision E-848). The screen
// follows the status, chat and log only while it is open (open() and close()); then the overview,
// players, chat, log, lists and settings follow the server's reports. Lost rights end following;
// rights given back while the screen is open follow again. Every action is a request of
// the channel; its answer shows as a notice, a refusal with a text for its reason (no rights,
// wrong details, missing confirmation, too soon). Commands and their output, the completion of a
// command line (QcAdminCompletion) and the help for its parameters serve the command page.
class QcAdminController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(AdminController)
    QML_UNCREATABLE("Provided by GameController.admin")
    // The server said this player is an operator: the pause menu offers "Manage server".
    Q_PROPERTY(bool operatorRights READ operatorRights NOTIFY operatorRightsChanged FINAL)
    // The name this player plays under (taking his own rights asks first).
    Q_PROPERTY(QString ownName READ ownName NOTIFY operatorRightsChanged FINAL)
    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged FINAL)
    // QcAdminViews::status; empty before the first status.
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged FINAL)
    // The charts show the day (one point a minute) rather than the last ten minutes.
    Q_PROPERTY(bool minutes READ minutes WRITE setMinutes NOTIFY chartsChanged FINAL)
    // QcAdminViews::charts of the chosen span; built only while the overview shows them.
    Q_PROPERTY(QVariantMap charts READ charts NOTIFY chartsChanged FINAL)
    Q_PROPERTY(bool chartsShown READ chartsShown WRITE setChartsShown NOTIFY chartsShownChanged FINAL)
    // The points that fill a chart's width: the seconds or minutes the server keeps.
    Q_PROPERTY(int chartCapacity READ chartCapacity NOTIFY chartsChanged FINAL)
    // The ticks a second the server aims at and the time one tick may take.
    Q_PROPERTY(int ticksPerSecond READ ticksPerSecond CONSTANT FINAL)
    Q_PROPERTY(int tickBudgetMs READ tickBudgetMs CONSTANT FINAL)
    // The lengths the server takes: a player's name, a chat or command line, a reason, a backup's
    // label (QcMsg::AdminLimits).
    Q_PROPERTY(int nameLength READ nameLength CONSTANT FINAL)
    Q_PROPERTY(int textLength READ textLength CONSTANT FINAL)
    Q_PROPERTY(int reasonLength READ reasonLength CONSTANT FINAL)
    Q_PROPERTY(int labelLength READ labelLength CONSTANT FINAL)
    // Counts the command tables received: bindings over complete() and help() read it to follow.
    Q_PROPERTY(int catalogueRevision READ catalogueRevision NOTIFY catalogueChanged FINAL)
    Q_PROPERTY(QVariantList players READ players NOTIFY playersChanged FINAL)
    Q_PROPERTY(QcAdminLineModel *chat READ chat CONSTANT FINAL)
    Q_PROPERTY(QcAdminLineModel *log READ log CONSTANT FINAL)
    // The lowest level of the log lines shown.
    Q_PROPERTY(LogLevel logLevel READ logLevel WRITE setLogLevel NOTIFY logLevelChanged FINAL)
    Q_PROPERTY(QVariantMap lists READ lists NOTIFY listsChanged FINAL)
    // QcAdminViews::groups of the game rules and of the server limits.
    Q_PROPERTY(QVariantList ruleGroups READ ruleGroups NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QVariantList limitGroups READ limitGroups NOTIFY settingsChanged FINAL)
    // Changed limits go to the server's server.json (else to the world's world.json).
    Q_PROPERTY(bool limitsFromServer READ limitsFromServer NOTIFY settingsChanged FINAL)
    // The changed settings not saved yet: {"rule:key" or "limit:key": value} (QcAdminSettingsDraft).
    Q_PROPERTY(QVariantMap draft READ draft NOTIFY draftChanged FINAL)
    Q_PROPERTY(bool dirty READ dirty NOTIFY draftChanged FINAL)
    // The command lines run and their answers, oldest first (refusals at the level Critical).
    Q_PROPERTY(QcAdminLineModel *output READ output CONSTANT FINAL)
    // The latest answer to an action; empty for none.
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged FINAL)
    Q_PROPERTY(Notice noticeKind READ noticeKind NOTIFY noticeChanged FINAL)

public:
    // The values follow QcMsg::AdminLogLine::Level.
    enum class LogLevel { Debug, Info, Warning, Critical };
    Q_ENUM(LogLevel)
    // What the notice says: nothing, an action done, or a refusal.
    enum class Notice { None, Done, Refused };
    Q_ENUM(Notice)
    using ContentSource = std::function<std::shared_ptr<const QcContentRegistry>()>;
    static constexpr qsizetype outputLimit = 200;

    explicit QcAdminController(QObject *parent = nullptr);

    // Uses `admin` (null: no session) and the blocks, items and creatures of `content` for the
    // completion; the screen closes.
    void follow(QcClientAdmin *admin, ContentSource content);
    void sessionStarted(QcClientSession *session);

    bool operatorRights() const { return m_admin && m_admin->operatorRights(); }
    QString ownName() const { return m_ownName; }
    bool isActive() const { return m_active; }
    QVariantMap status() const { return m_status; }
    bool minutes() const { return m_minutes; }
    void setMinutes(bool minutes);
    QVariantMap charts() const { return m_charts; }
    bool chartsShown() const { return m_chartsShown; }
    void setChartsShown(bool shown);
    int chartCapacity() const;
    static int ticksPerSecond();
    static int tickBudgetMs();
    static int nameLength();
    static int textLength();
    static int reasonLength();
    static int labelLength();
    int catalogueRevision() const { return m_catalogueRevision; }
    QVariantList players() const { return m_players; }
    QcAdminLineModel *chat() { return &m_chat; }
    QcAdminLineModel *log() { return &m_log; }
    LogLevel logLevel() const { return m_logLevel; }
    void setLogLevel(LogLevel level);
    QVariantMap lists() const { return m_lists; }
    QVariantList ruleGroups() const { return m_ruleGroups; }
    QVariantList limitGroups() const { return m_limitGroups; }
    bool limitsFromServer() const;
    QVariantMap draft() const { return m_draft.values(); }
    bool dirty() const { return !m_draft.isEmpty(); }
    QcAdminLineModel *output() { return &m_output; }
    QString notice() const { return m_notice; }
    Notice noticeKind() const { return m_noticeKind; }

    // The screen opened: follows status, chat and log and asks for the commands, lists and settings.
    Q_INVOKABLE void open();
    // The screen closed: follows nothing any more.
    Q_INVOKABLE void close();

    Q_INVOKABLE void kick(const QString &name, const QString &reason);
    Q_INVOKABLE void ban(const QString &name, const QString &reason);
    Q_INVOKABLE void unban(const QString &name);
    Q_INVOKABLE void op(const QString &name);
    Q_INVOKABLE void deop(const QString &name);
    Q_INVOKABLE void whitelistAdd(const QString &name);
    Q_INVOKABLE void whitelistRemove(const QString &name);
    Q_INVOKABLE void setWhitelist(bool enabled);
    // Puts a game rule (`limit` false) or a server limit at `value` into the draft.
    Q_INVOKABLE void changeSetting(bool limit, const QString &key, qint64 value);
    // As changeSetting with the field's `text` (QcAdminSettingsDraft::parsed); false, and the draft
    // unchanged, when it is no value of the setting.
    Q_INVOKABLE bool changeSettingText(bool limit, const QString &key, const QString &text);
    // Sends each value of the draft; a value stays in it until the server took it, and one notice
    // sums up all answers.
    Q_INVOKABLE void saveSettings();
    Q_INVOKABLE void discardSettings();
    Q_INVOKABLE void save();
    // A backup named `label` (one word; empty for the date).
    Q_INVOKABLE void backup(const QString &label);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void sendChat(const QString &text);
    Q_INVOKABLE void runCommand(const QString &line);
    Q_INVOKABLE void clearOutput();
    Q_INVOKABLE void clearNotice();

    // The lines `line` may become by completing its last word (QcAdminCompletion::complete), at
    // most eight.
    Q_INVOKABLE QStringList complete(const QString &line) const;
    // Help for the command of `line`: {command, usage, description, next} (QcAdminViews::help);
    // empty when the first word names no command.
    Q_INVOKABLE QVariantMap help(const QString &line) const;

    // The text for a refusal with `reason`, followed by the server's `text` where it says more.
    static QString refusalText(QcClientAdmin::Refusal reason, const QString &text);

signals:
    void operatorRightsChanged();
    void activeChanged();
    void statusChanged();
    void chartsChanged();
    void chartsShownChanged();
    void catalogueChanged();
    void draftChanged();
    void playersChanged();
    void logLevelChanged();
    void listsChanged();
    void settingsChanged();
    void noticeChanged();

private:
    // A request sent and not answered yet.
    struct Pending
    {
        // A command line: its answer goes to the output.
        bool command = false;
        // A value of the draft being saved (QcAdminSettingsDraft::id); empty for other actions.
        QString setting;
        qint64 value = 0;
    };

    void connectAdmin();
    // Follows status, chat and log and asks for the commands, lists and settings.
    void subscribe();
    void onRightsChanged();
    void showChat();
    void showLog();
    // The answer to saving one value of the draft; the last one sums them up as the notice.
    void onSettingSaved(const Pending &pending, const QcMsg::AdminResult &result);
    // Sends a request of an action; its answer becomes the notice.
    void act(QcClientAdmin::Kind kind, const QStringList &arguments);
    void onResult(quint32 id);
    void onRefused(QcClientAdmin::Refusal reason, const QString &text);
    void setNotice(Notice kind, const QString &text);
    // Lines of the command output at `level` (QcMsg::AdminLogLine::Level).
    void appendOutput(const QStringList &lines, int level);
    void updateStatus();
    // Builds the charts when shown and when their points changed (or always when `force`).
    void updateCharts(bool force = false);
    void updatePlayers();
    void updateLists();
    void updateSettings();

    QPointer<QcClientAdmin> m_admin;
    ContentSource m_content;
    bool m_active = false;
    bool m_minutes = false;
    bool m_chartsShown = false;
    // The newest point and the number of points the charts were built from; -1 to build anew.
    std::pair<qint64, qsizetype> m_chartsBuilt{-1, 0};
    int m_catalogueRevision = 0;
    QString m_ownName;
    QcAdminSettingsDraft m_draft;
    // Values of the draft saved since the last notice, and the refusals among them.
    int m_settingsSaved = 0;
    QStringList m_settingRefusals;
    LogLevel m_logLevel = LogLevel::Info;
    QVariantMap m_status;
    QVariantMap m_charts;
    QVariantList m_players;
    QcAdminLineModel m_chat;
    QcAdminLineModel m_log;
    // What the models show of the channel's chat and log.
    QcClientAdmin::Arrivals m_chatSeen;
    QcClientAdmin::Arrivals m_logSeen;
    QVariantMap m_lists;
    QVariantList m_ruleGroups;
    QVariantList m_limitGroups;
    QcAdminLineModel m_output;
    QString m_notice;
    Notice m_noticeKind = Notice::None;
    QHash<quint32, Pending> m_pending;
};
