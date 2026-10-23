#include "app/admin/QcAdminViews.h"

#include "app/admin/QcAdminSettingNames.h"
#include "clientcore/admin/QcAdminCompletion.h"
#include "common/protocol/QcChatText.h"

#include <QCoreApplication>
#include <QLocale>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// A game mode as the player list names it.
QString modeName(QcGameMode mode)
{
    switch (mode) {
    case QcGameMode::Creative:
        return QCoreApplication::translate("QcAdminViews", "Creative");
    case QcGameMode::Adventure:
        return QCoreApplication::translate("QcAdminViews", "Adventure");
    case QcGameMode::Spectator:
        return QCoreApplication::translate("QcAdminViews", "Spectator");
    case QcGameMode::Survival:
        break;
    }
    return QCoreApplication::translate("QcAdminViews", "Survival");
}

// The phases shown on the overview: the slowest few.
constexpr qsizetype shownPhases = 5;

QVariantList phases(const QcMsg::AdminStatus &status)
{
    QList<std::pair<float, const char *>> timed;
    for (int phase = 0; phase < QcTickPhases::count; ++phase) {
        timed.append({status.phaseMeanMs[static_cast<std::size_t>(phase)], QcTickPhases::ids[static_cast<std::size_t>(phase)]});
    }
    std::ranges::stable_sort(timed, std::ranges::greater{}, &std::pair<float, const char *>::first);

    QVariantList shown;
    for (qsizetype i = 0; i < std::min(shownPhases, timed.size()); ++i) {
        shown.append(QVariantMap{{u"name"_s, QString::fromLatin1(timed[i].second)}, {u"ms"_s, timed[i].first}});
    }
    return shown;
}

// One series of the history: `value` of every sample, oldest first.
template<typename Value>
QVariantList series(const QList<QcMsg::AdminSample> &samples, Value value)
{
    QVariantList values;
    values.reserve(samples.size());
    for (const QcMsg::AdminSample &sample : samples) {
        values.append(static_cast<qreal>(value(sample)));
    }
    return values;
}

QStringList sorted(QStringList names)
{
    names.sort(Qt::CaseInsensitive);
    return names;
}

QString translated(const QString &text, bool limits)
{
    return QCoreApplication::translate(limits ? "QcServerLimits" : "QcGameRules", text.toUtf8().constData());
}

QVariantMap setting(const QcMsg::AdminSettings::Setting &entry, bool limits)
{
    const QLocale locale;
    const QString name = QcAdminSettingNames::name(entry.key, limits);
    return {{u"key"_s, entry.key},
            {u"name"_s, name.isEmpty() ? entry.key : name},
            {u"named"_s, !name.isEmpty()},
            {u"limit"_s, limits},
            {u"lowText"_s, locale.toString(entry.low)},
            {u"highText"_s, locale.toString(entry.high)},
            {u"defaultText"_s, locale.toString(entry.defaultValue)},
            {u"description"_s, translated(entry.description, limits)},
            {u"boolean"_s, entry.type == QcMsg::AdminSettings::Type::Boolean},
            {u"low"_s, entry.low},
            {u"high"_s, entry.high},
            {u"defaultValue"_s, entry.defaultValue},
            {u"value"_s, entry.value}};
}

// A parameter as the help shows it.
QString shown(const QcCommandSyntax::Parameter &parameter)
{
    using Kind = QcCommandSyntax::Parameter::Kind;
    QString name = parameter.choices.isEmpty() ? parameter.name : parameter.choices.join(u'|');
    switch (parameter.kind) {
    case Kind::Literal:
        return name;
    case Kind::Required:
        return parameter.choices.isEmpty() ? u"<%1>"_s.arg(name) : name;
    case Kind::Optional:
        return u"[%1]"_s.arg(name);
    }
    return name;
}

} // namespace

namespace QcAdminViews {

QVariantMap status(const QcMsg::AdminStatus &status)
{
    const QcMsg::AdminSample &sample = status.sample;
    return {{u"tps"_s, sample.ticksPerSecond},
            {u"tickMean"_s, sample.tickMeanMs},
            {u"tickP99"_s, sample.tickP99Ms},
            {u"tickMax"_s, sample.tickMaxMs},
            {u"players"_s, sample.players},
            {u"chunks"_s, sample.loadedChunks},
            {u"entities"_s, sample.entities},
            {u"memory"_s, sample.memoryMb},
            {u"peakMemory"_s, status.peakMemoryMb},
            {u"sent"_s, sample.sentKbPerSecond},
            {u"received"_s, sample.receivedKbPerSecond},
            {u"uptimeMs"_s, static_cast<qreal>(status.uptimeMs)},
            {u"tick"_s, static_cast<qreal>(status.tick)},
            {u"skipped"_s, static_cast<qreal>(status.skippedTicks)},
            {u"world"_s, status.worldName},
            {u"seed"_s, QString::number(status.seed)},
            {u"generator"_s, status.generator},
            {u"version"_s, status.version},
            {u"protocol"_s, status.protocol},
            {u"phases"_s, phases(status)}};
}

QVariantMap charts(const QcMsg::AdminHistory &history, bool minutes)
{
    const QList<QcMsg::AdminSample> &samples = minutes ? history.minutes : history.seconds;
    using Sample = QcMsg::AdminSample;
    return {{u"tps"_s, series(samples, [](const Sample &s) { return s.ticksPerSecond; })},
            {u"tickMean"_s, series(samples, [](const Sample &s) { return s.tickMeanMs; })},
            {u"tickP99"_s, series(samples, [](const Sample &s) { return s.tickP99Ms; })},
            {u"players"_s, series(samples, [](const Sample &s) { return s.players; })},
            {u"chunks"_s, series(samples, [](const Sample &s) { return s.loadedChunks; })},
            {u"entities"_s, series(samples, [](const Sample &s) { return s.entities; })},
            {u"memory"_s, series(samples, [](const Sample &s) { return s.memoryMb; })},
            {u"sent"_s, series(samples, [](const Sample &s) { return s.sentKbPerSecond; })},
            {u"received"_s, series(samples, [](const Sample &s) { return s.receivedKbPerSecond; })}};
}

QVariantList players(const QList<QcMsg::AdminPlayers::Entry> &players)
{
    QList<QcMsg::AdminPlayers::Entry> byName = players;
    std::ranges::sort(byName, [](const auto &a, const auto &b) { return a.name.compare(b.name, Qt::CaseInsensitive) < 0; });
    QVariantList shown;
    for (const QcMsg::AdminPlayers::Entry &entry : std::as_const(byName)) {
        shown.append(QVariantMap{{u"name"_s, entry.name},
                                 {u"ping"_s, entry.pingMs},
                                 {u"gameMode"_s, static_cast<int>(entry.gameMode)},
                                 {u"modeName"_s, modeName(entry.gameMode)},
                                 {u"operator"_s, entry.operatorRights},
                                 {u"local"_s, entry.local},
                                 {u"address"_s, entry.address},
                                 {u"connectedMs"_s, static_cast<qreal>(entry.connectedMs)}});
    }
    return shown;
}

QVariantMap lists(const QcMsg::AdminLists &lists)
{
    QList<QcMsg::AdminLists::Ban> bans = lists.bans;
    std::ranges::sort(bans, [](const auto &a, const auto &b) { return a.name.compare(b.name, Qt::CaseInsensitive) < 0; });
    QVariantList banned;
    for (const QcMsg::AdminLists::Ban &ban : std::as_const(bans)) {
        banned.append(QVariantMap{{u"name"_s, ban.name}, {u"reason"_s, ban.reason}, {u"by"_s, ban.by}, {u"sinceMs"_s, static_cast<qreal>(ban.sinceMs)}});
    }
    return {{u"whitelistEnabled"_s, lists.whitelistEnabled},
            {u"operators"_s, sorted(lists.operators)},
            {u"whitelist"_s, sorted(lists.whitelist)},
            {u"bans"_s, banned}};
}

QVariantList groups(const QList<QcMsg::AdminSettings::Setting> &settings, bool limits)
{
    QStringList order;
    QHash<QString, QVariantList> members;
    for (const QcMsg::AdminSettings::Setting &entry : settings) {
        if (!members.contains(entry.group)) {
            order.append(entry.group);
        }
        members[entry.group].append(setting(entry, limits));
    }

    QVariantList grouped;
    for (const QString &group : std::as_const(order)) {
        grouped.append(QVariantMap{{u"title"_s, translated(group, limits)}, {u"settings"_s, members.value(group)}});
    }
    return grouped;
}

QVariantMap help(const QString &line, const QcMsg::AdminCatalogue &catalogue)
{
    const QStringList words = line.trimmed().split(u' ', Qt::SkipEmptyParts);
    const QString name = words.isEmpty() ? QString() : (words[0].startsWith(u'/') ? words[0].mid(1) : words[0]);
    const QcMsg::AdminCatalogue::Command *command = QcAdminCompletion::command(catalogue, name);
    if (!command) {
        return {};
    }

    QStringList next;
    const QList<QcCommandSyntax::Parameter> parameters = QcAdminCompletion::parameters(line, catalogue);
    for (const QcCommandSyntax::Parameter &parameter : parameters) {
        const QString text = shown(parameter);
        if (!next.contains(text)) {
            next.append(text);
        }
    }
    // Macros of the content have plain texts, the registry's commands translation keys.
    const auto text = [command](const QString &source) {
        return command->macro ? source : QCoreApplication::translate("QcServerMessages", source.toUtf8().constData());
    };
    return {{u"command"_s, command->name},
            {u"usage"_s, text(command->usage)},
            {u"description"_s, text(command->description)},
            {u"next"_s, next.join(u" / "_s)}};
}

QList<QcAdminLineModel::Line> chatLines(const QList<QcMsg::AdminChatLine> &chat)
{
    QList<QcAdminLineModel::Line> lines;
    lines.reserve(chat.size());
    for (const QcMsg::AdminChatLine &line : chat) {
        lines.append({line.timeMs, static_cast<int>(QcMsg::AdminLogLine::Level::Info), line.sender, QcChatText::display(line.message)});
    }
    return lines;
}

QList<QcAdminLineModel::Line> logLines(const QList<QcMsg::AdminLogLine> &log)
{
    QList<QcAdminLineModel::Line> lines;
    lines.reserve(log.size());
    for (const QcMsg::AdminLogLine &line : log) {
        lines.append({line.timeMs, static_cast<int>(line.level), line.category, line.text});
    }
    return lines;
}

} // namespace QcAdminViews
