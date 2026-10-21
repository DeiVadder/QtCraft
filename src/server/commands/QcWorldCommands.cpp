#include "server/commands/QcWorldCommands.h"

#include "common/core/QcProcessMemory.h"
#include "common/protocol/QcChatText.h"
#include "server/blockentities/QcBlockEntityChecks.h"
#include "server/commands/QcCommandArguments.h"
#include "server/commands/QcPendingCommands.h"
#include "server/commands/QcWorldBackup.h"
#include "server/core/QcServerLogging.h"
#include "server/core/QcServerState.h"
#include "server/entities/QcEntityChecks.h"
#include "server/tick/QcTickPhaseTimer.h"
#include "server/tick/QcTickScheduler.h"

#include <QElapsedTimer>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

const QStringList kinds{u"mobs"_s, u"hostile"_s, u"animals"_s, u"ambient"_s, u"items"_s, u"stands"_s, u"all"_s};

bool inRange(const std::optional<QcVec3> &centre, double radius, const QcVec3 &position)
{
    return !centre || (position - *centre).length() <= radius;
}

QString megabytes(double value)
{
    return QString::number(value, 'f', 0);
}

} // namespace

QcWorldCommands::QcWorldCommands(QcServerState &state, QcPendingCommands &pending, Parts parts)
    : m_state(state)
    , m_pending(pending)
    , m_parts(std::move(parts))
{
}

void QcWorldCommands::addTo(QcCommandRegistry &registry)
{
    registry.addBuiltIn({.name = u"save"_s,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /save"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Saves the world now."),
                         .run = [this](const QcCommandContext &context) { save(context); }});
    registry.addBuiltIn({.name = u"backup"_s,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /backup [label]"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages",
                                                          "Saves the world and copies its folder into the backups folder, named with date and time."),
                         .run = [this](const QcCommandContext &context) { backup(context); }});
    registry.addBuiltIn({.name = u"worldinfo"_s,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /worldinfo"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Shows chunks, entities, tick times and memory of the server."),
                         .run = [this](const QcCommandContext &context) { worldInfo(context); }});
    registry.addBuiltIn({.name = u"limits"_s,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /limits"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages",
                                                          "Shows the simulation limits of the server (server.json or world.json, \"limits\")."),
                         .run = [this](const QcCommandContext &context) { limits(context); }});
    registry.addBuiltIn({.name = u"remove"_s, .minArguments = 1,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages",
                                                    "Usage: /remove <mobs|hostile|animals|ambient|items|stands|all|<creature>|item:<item>> [radius|world]"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages",
                                                          "Removes entities around you (32 m unless given, at most 256 m) or in the whole loaded world; "
                                                          "'all' means creatures and items."),
                         .run = [this](const QcCommandContext &context) { remove(context); }});
    registry.addBuiltIn({.name = u"stop"_s, .permission = QcPermission::Console,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /stop"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Saves the world and stops the server."),
                         .run = [this](const QcCommandContext &context) {
                             context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Stopping the server"));
                             m_parts.stop();
                         }});
}

void QcWorldCommands::save(const QcCommandContext &context) const
{
    const QcStatus saved = m_parts.save();
    if (saved) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "World saved"));
    } else {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Saving failed: %1"), {saved.error()});
    }
}

void QcWorldCommands::backup(const QcCommandContext &context)
{
    const QString label = context.args.value(0);
    if (m_state.config().worldDirectory.isEmpty() || !QcWorldBackup::isValidLabel(label) || context.args.size() > 1) {
        context.reply(m_state.config().worldDirectory.isEmpty() ? QT_TRANSLATE_NOOP("QcServerMessages", "This world is not saved, there is nothing to back up")
                                                              : QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /backup [label]"));
        return;
    }
    if (m_backups.running()) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "A backup is still being copied, try again when it is done"));
        return;
    }

    // Saved first, so the copy holds the world as it is now.
    if (const QcStatus saved = m_parts.save(); !saved) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Saving failed: %1"), {saved.error()});
        return;
    }

    // The snapshot freezes the saved files at once; the copy runs in the background (E-256).
    if (!m_clock.isValid()) {
        m_clock.start();
    }
    const qint64 started = m_clock.elapsed();
    const QString root = m_state.config().backupDirectory.isEmpty() ? QcWorldBackup::defaultRoot(m_state.config().worldDirectory)
                                                                  : m_state.config().backupDirectory;
    QcResult<QcWorldBackup::Snapshot> snapshot = QcWorldBackup::snapshot(m_state.config().worldDirectory, root, label, QDateTime::currentDateTime());
    if (!snapshot) {
        qCWarning(lcCommands).noquote() << u"Backup failed: %1"_s.arg(snapshot.error());
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Backup failed: %1"), {snapshot.error()});
        return;
    }

    qCInfo(lcCommands).noquote() << u"Backup snapshot of %1 files %2 in %3 ms, copying to %4"_s.arg(snapshot->files.size())
                                        .arg(snapshot->linked ? u"linked"_s : u"copied"_s)
                                        .arg(m_clock.elapsed() - started)
                                        .arg(snapshot->target);
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Backup of %1 files started, it is copied to %2"),
                  {QString::number(snapshot->files.size()), snapshot->target});
    const quint32 requester = context.source.id();
    m_backups.start(std::move(*snapshot), [this, requester, started](const QcResult<QcWorldBackup::Summary> &result) {
        reportBackup(requester, result, started);
    });
}

void QcWorldCommands::reportBackup(quint32 requester, const QcResult<QcWorldBackup::Summary> &result, qint64 startedMs) const
{
    QcMsg::ChatMessage line;
    if (result) {
        qCInfo(lcCommands).noquote() << u"Backup of %1 files (%2 bytes) in %3 ms: %4"_s.arg(result->files).arg(result->bytes)
                                            .arg(m_clock.elapsed() - startedMs)
                                            .arg(result->directory);
        line = QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "Backup with %1 files (%2 MB) in %3"),
                                   {QString::number(result->files), megabytes(double(result->bytes) / (1024.0 * 1024.0)), result->directory});
    } else {
        qCWarning(lcCommands).noquote() << u"Backup failed: %1"_s.arg(result.error());
        line = QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "Backup failed: %1"), {result.error()});
    }

    // The player who asked hears of it if he is still there; the console reads the log.
    for (const auto &client : m_state.clients()) {
        if (requester != 0 && client->id == requester && client->state == QcServerClient::State::Playing) {
            client->connection->send(line);
        }
    }
}

void QcWorldCommands::limits(const QcCommandContext &context) const
{
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Limits from %1:"),
                  {m_state.config().limitsFromServer ? u"server.json"_s : u"world.json"_s});
    const QStringList lines = m_state.config().limits.lines();
    for (const QString &line : lines) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "  %1"), {line});
    }
}

void QcWorldCommands::worldInfo(const QcCommandContext &context) const
{
    const QcPhaseWindow &window = m_parts.phases.window();
    int slowest = 0;
    for (int phase = 1; phase < QcTickPhases::inTick; ++phase) {
        slowest = window.mean(phase) > window.mean(slowest) ? phase : slowest;
    }
    const auto ms = [](double value) { return QString::number(value, 'f', 2); };

    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "World %1 (%2, seed %3), tick %4"),
                  {m_state.config().worldName, m_state.config().generator, QString::number(m_state.config().seed), QString::number(m_state.clock().tick())});
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Chunks: %1 loaded, %2 stored; players: %3"),
                  {QString::number(m_state.world().loadedChunkCount()), QString::number(m_state.world().saves().storedChunkCount()),
                   QString::number(std::ranges::count_if(m_state.clients(), [](const auto &client) { return client->joined; }))});
    // The rules of the entity register (decision E-364); broken ones go to the log too.
    const QStringList entityProblems = QcEntityChecks::problems(m_state.entities());
    for (const QString &problem : entityProblems) {
        qCWarning(lcCommands) << "Entities:" << problem;
    }
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Entities: %1 creatures, %2 items, %3 falling blocks, %4 armour stands, %5 broken rules; %6 machines, %7 chests"),
                  {QString::number(m_state.entities().creatures().awakeCount()), QString::number(m_state.entities().items().items().size()),
                   QString::number(m_state.entities().fallingBlocks().all().size()), QString::number(m_state.entities().stands().stands().size()),
                   QString::number(entityProblems.size()), QString::number(m_state.blockEntities().machineCount()),
                   QString::number(m_state.blockEntities().containerCount())});
    // The rules between blocks and block entities (decision E-357); broken ones go to the log too.
    const QStringList problems = QcBlockEntityChecks::problems(m_state.world());
    for (const QString &problem : problems) {
        qCWarning(lcCommands) << "Block entities:" << problem;
    }
    const QcBlockEntities &entities = m_state.blockEntities();
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Block entities: %1 containers, %2 machines, %3 saplings, %4 scheduled ticks; %5 broken rules"),
                  {QString::number(entities.containerCount()), QString::number(entities.machineCount()), QString::number(entities.saplingCount()),
                   QString::number(entities.schedule().size()), QString::number(problems.size())});
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Tick: %1 ms mean, %2 ms max, %3 ms late on average, %4 skipped; slowest phase %5 (%6 ms)"),
                  {ms(window.mean(QcTickPhaseTimer::durationSeries)), ms(window.max(QcTickPhaseTimer::durationSeries)),
                   ms(window.mean(QcTickPhaseTimer::lateSeries)), QString::number(m_parts.ticks.stats().skipped()),
                   QString::fromLatin1(QcTickPhases::ids[std::size_t(slowest)]), ms(window.mean(slowest))});
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Memory: %1 MB now, %2 MB at most"),
                  {megabytes(QcProcessMemory::currentMb()), megabytes(QcProcessMemory::peakMb())});
}

bool QcWorldCommands::isKnownSelector(const QString &what) const
{
    if (what.startsWith(u"item:"_s)) {
        return m_state.content().itemId(QcCommandArguments::qualified(what.mid(5))).has_value();
    }
    return kinds.contains(what) || m_state.content().mobIndex(QcCommandArguments::qualified(what)).has_value();
}

bool QcWorldCommands::matchesMob(const QString &what, int type) const
{
    const QcMobDefinition &mob = m_state.content().mobs()[type];
    if (what == u"mobs"_s || what == u"all"_s) {
        return true;
    }
    if (what == u"hostile"_s || what == u"animals"_s || what == u"ambient"_s) {
        const bool animal = !mob.hostile && !mob.ambient;
        return (what == u"hostile"_s && mob.hostile) || (what == u"animals"_s && animal) || (what == u"ambient"_s && mob.ambient);
    }
    return !kinds.contains(what) && !what.startsWith(u"item:"_s) && m_state.content().mobIndex(QcCommandArguments::qualified(what)) == type;
}

bool QcWorldCommands::matchesItem(const QString &what, const QcItemStack &stack) const
{
    if (what == u"items"_s || what == u"all"_s) {
        return true;
    }
    return what.startsWith(u"item:"_s) && m_state.content().itemId(QcCommandArguments::qualified(what.mid(5))) == stack.item;
}

QcWorldCommands::Swept QcWorldCommands::sweep(const Removal &removal, bool apply)
{
    QList<quint32> mobs;
    for (const QcCreature &mob : m_state.entities().creatures().mobs()) {
        if (!mob.dormant && matchesMob(removal.what, mob.type) && inRange(removal.centre, removal.radius, mob.motion.position)) {
            mobs.append(mob.id);
        }
    }

    QList<quint32> stands;
    for (const QcArmourStand &stand : m_state.entities().stands().stands()) {
        if (removal.what == u"stands"_s && inRange(removal.centre, removal.radius, stand.motion.position)) {
            stands.append(stand.id);
        }
    }

    const auto itemMatches = [&](const QcItemEntity &item) {
        return matchesItem(removal.what, item.stack) && inRange(removal.centre, removal.radius, item.motion.position);
    };
    const Swept swept{int(mobs.size()), int(std::ranges::count_if(m_state.entities().items().items(), itemMatches)), int(stands.size())};
    if (!apply) {
        return swept;
    }

    // Removed without dying: no drops; the clients drop them with the next entity sync.
    for (const quint32 id : std::as_const(mobs)) {
        m_state.entities().creatures().remove(id);
    }
    for (const quint32 id : std::as_const(stands)) {
        m_state.entities().stands().remove(id);
    }
    m_state.entities().items().removeWhere(itemMatches);
    return swept;
}

void QcWorldCommands::remove(const QcCommandContext &context)
{
    const QStringList &args = context.args;
    Removal removal{args[0].toLower(), std::nullopt, 0.0};
    const bool world = args.value(1) == u"world"_s;
    const std::optional<int> radius = world ? std::optional<int>(0) : QcCommandArguments::count(args, 1, defaultRemoveRadius, 1, maxRemoveRadius);
    const bool known = isKnownSelector(removal.what);
    if (!radius || !known || args.size() > 2 || (!world && !context.source.player())) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages",
                                        "Usage: /remove <mobs|hostile|animals|ambient|items|stands|all|<creature>|item:<item>> [radius|world]"));
        return;
    }
    if (!world) {
        removal.centre = context.source.player()->motion.position;
        removal.radius = *radius;
    }

    const Swept found = sweep(removal, false);
    auto run = [this, removal](const QcCommandSource &source) {
        const Swept removed = sweep(removal, true);
        source.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Removed %1 creatures, %2 item stacks and %3 armour stands"),
                     {QString::number(removed.mobs), QString::number(removed.items), QString::number(removed.stands)});
    };
    if (found.total() <= confirmRemoveCount && found.stands == 0 && !world) {
        run(context.source);
        return;
    }

    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "This removes %1 creatures, %2 item stacks and %3 armour stands."),
                  {QString::number(found.mobs), QString::number(found.items), QString::number(found.stands)});
    m_pending.ask(context, run, m_state.clock().playerTimeNs());
}
