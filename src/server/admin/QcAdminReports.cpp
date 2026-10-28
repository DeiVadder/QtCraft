#include "server/admin/QcAdminReports.h"

#include "common/core/QcProcessMemory.h"
#include "common/diagnostics/QcBuildInfo.h"
#include "common/diagnostics/QcMessageLog.h"
#include "common/protocol/QcProtocol.h"
#include "server/commands/QcCommandRegistry.h"
#include "server/commands/QcGameRules.h"
#include "server/core/QcServerLogging.h"
#include "server/core/QcServerState.h"
#include "server/tick/QcTickPhaseTimer.h"
#include "server/tick/QcTickScheduler.h"

#include <QCoreApplication>

using namespace Qt::StringLiterals;
namespace Limits = QcMsg::AdminLimits;

QcAdminReports::QcAdminReports(const QcServerState &state, const QcCommandRegistry &registry, const QcTickPhaseTimer &phases,
                               const QcTickScheduler &ticks)
    : m_state(state)
    , m_registry(registry)
    , m_phases(phases)
    , m_ticks(ticks)
{
}

QcMsg::AdminStatus QcAdminReports::status(const QcMsg::AdminSample &sample, quint64 uptimeMs) const
{
    QcMsg::AdminStatus status;
    status.sample = sample;
    const QcPhaseWindow &window = m_phases.window();
    for (int phase = 0; phase < QcTickPhases::count; ++phase) {
        status.phaseMeanMs[static_cast<std::size_t>(phase)] = static_cast<float>(window.mean(phase));
    }
    status.entities = entitiesByKind();
    status.peakMemoryMb = static_cast<float>(QcProcessMemory::peakMb());
    status.uptimeMs = uptimeMs;
    status.tick = m_state.clock().tick();
    status.skippedTicks = m_ticks.stats().skipped();
    status.worldName = m_state.config().worldName.left(Limits::maxText);
    status.seed = m_state.config().seed;
    status.generator = m_state.config().generator.left(Limits::maxText);
    status.version = QcBuildInfo::clientText(QCoreApplication::applicationName()).left(Limits::maxText);
    status.protocol = QcProtocol::version;
    return status;
}

std::array<quint32, QcEntityKinds::count> QcAdminReports::entitiesByKind() const
{
    std::array<quint32, QcEntityKinds::count> counts{};
    m_state.entities().forEach([&counts](const QcEntity &entity) { ++counts[static_cast<std::size_t>(entity.kind)]; });
    return counts;
}

QcMsg::AdminPlayers QcAdminReports::players(const QHash<quint32, qint32> &pings) const
{
    QcMsg::AdminPlayers report;
    for (const auto &client : m_state.clients()) {
        if (!client->joined || client->state != QcServerClient::State::Playing || std::cmp_greater_equal(report.players.size(), Limits::maxPlayers)) {
            continue;
        }
        QcMsg::AdminPlayers::Entry entry;
        entry.entityId = client->id;
        entry.name = client->name;
        entry.pingMs = pings.value(client->id, -1);
        entry.position = client->motion.position;
        entry.gameMode = client->gameMode;
        entry.operatorRights = m_state.permissionOf(*client) != QcPermission::Player;
        entry.local = client->local;
        entry.connectedMs = client->login.connected.isValid() ? client->login.connected.elapsed() : 0;
        entry.address = client->login.address;
        report.players.append(entry);
    }
    return report;
}

QcMsg::AdminCatalogue QcAdminReports::catalogue() const
{
    QcMsg::AdminCatalogue report;
    const QList<const QcCommandSpec *> commands = m_registry.commands();
    for (const QcCommandSpec *spec : commands) {
        QStringList names = spec->names ? spec->names() : QStringList();
        if (std::cmp_greater(names.size(), Limits::maxNames)) {
            if (!m_namesCut.contains(spec->name)) {
                m_namesCut.insert(spec->name);
                qCWarning(lcAdmin).noquote() << u"Command catalogue: /%1 has %2 names, sending the first %3"_s.arg(spec->name).arg(names.size()).arg(Limits::maxNames);
            }
            names = names.mid(0, Limits::maxNames);
        }
        report.commands.append({spec->name, spec->aliases.mid(0, Limits::maxAliases), static_cast<quint8>(spec->permission), spec->needsPlayer, false,
                                QString::fromUtf8(spec->usage), QString::fromUtf8(spec->description), static_cast<quint8>(spec->category), names});
    }
    for (const QcCommandMacroDefinition &macro : m_state.content().commandMacros()) {
        if (!m_registry.find(macro.name) && std::cmp_less(report.commands.size(), Limits::maxCommands)) {
            report.commands.append({macro.name, {}, static_cast<quint8>(macro.operatorOnly ? QcPermission::Operator : QcPermission::Player), false, true,
                                    macro.usage, macro.description, static_cast<quint8>(QcCommandCategory::Other), {}});
        }
    }
    return report;
}

QcMsg::AdminLists QcAdminReports::lists() const
{
    const QcAccessList &access = m_state.access();
    QcMsg::AdminLists report;
    report.whitelistEnabled = access.whitelistEnabled();
    report.operators = access.operators().mid(0, Limits::maxEntries);
    report.whitelist = access.whitelist().mid(0, Limits::maxEntries);
    const QList<QcAccessList::Ban> bans = access.bans().mid(0, Limits::maxEntries);
    for (const QcAccessList::Ban &ban : bans) {
        report.bans.append({ban.name, ban.reason, ban.by, ban.since.isValid() ? ban.since.toMSecsSinceEpoch() : 0});
    }
    return report;
}

QcMsg::AdminSettings QcAdminReports::settings() const
{
    using Type = QcMsg::AdminSettings::Type;
    QcMsg::AdminSettings report;
    for (const QcGameRules::Rule &rule : QcGameRules::all()) {
        report.rules.append({rule.name.toString(), QString::fromUtf8(rule.group), QString::fromUtf8(rule.description),
                             rule.flag ? Type::Boolean : Type::Integer, 0, rule.highest, rule.defaultValue, QcGameRules::value(rule, m_state.info())});
    }
    const QcServerLimits defaults;
    const QcServerLimits &limits = m_state.config().limits;
    for (const QcServerLimits::Field &field : QcServerLimits::fields()) {
        report.limits.append({QString::fromLatin1(field.key), QString::fromUtf8(field.group), QString::fromUtf8(field.description), Type::Integer,
                              field.low, field.high, defaults.*(field.value), limits.*(field.value)});
    }
    report.limitsFromServer = m_state.config().limitsFromServer;
    return report;
}

const QSet<QString> &QcAdminReports::serverCategories()
{
    // Not qtcraft.worlds and qtcraft.content: they name folders of the host's computer; not the
    // client's, the app's or the health line, which are the host's game.
    static const QSet<QString> categories{u"qtcraft.admin"_s,   u"qtcraft.commands"_s, u"qtcraft.gamemodes"_s, u"qtcraft.items"_s,
                                          u"qtcraft.lag"_s,     u"qtcraft.network"_s,  u"qtcraft.plants"_s,    u"qtcraft.server"_s,
                                          u"qtcraft.trades"_s, u"qtcraft.worldgen"_s};
    return categories;
}

QcAdminReports::Log QcAdminReports::log(QcMsg::AdminLogLine::Level level, qsizetype count, quint64 after, bool tail) const
{
    const QSet<QString> &categories = m_state.config().fullAdminLog ? QSet<QString>{} : serverCategories();
    const QcMessageLog::Recent recent = QcMessageLog::recent(static_cast<int>(level), std::min<qsizetype>(count, Limits::maxLogLines), after, categories);
    Log report{{tail, {}}, recent.latest};
    for (const QcMessageLog::RecentLine &line : recent.lines) {
        report.message.lines.append({line.timeMs, static_cast<QcMsg::AdminLogLine::Level>(line.severity), line.category.left(Limits::maxText), line.text});
    }
    return report;
}
