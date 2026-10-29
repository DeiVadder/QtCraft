// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/commands/QcTickCommands.h"

#include "common/physics/QcPhysics.h"
#include "common/protocol/QcChatText.h"
#include "common/world/QcDayCycle.h"
#include "server/core/QcServerLogging.h"
#include "server/core/QcServerState.h"
#include "server/tick/QcTickControl.h"
#include "server/tick/QcTickPhaseTimer.h"
#include "server/tick/QcTickScheduler.h"


#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

const char *const usage = QT_TRANSLATE_NOOP("QcServerMessages",
                                            "Usage: /tick <query|freeze|unfreeze|step [ticks]|rate <ticks per second|reset>|"
                                            "sprint <ticks|seconds s|minutes min|hours h|days d|stop>>");

QString number(double value)
{
    return QString::number(value, 'f', 1);
}

} // namespace

QcTickCommands::QcTickCommands(QcServerState &state, Parts parts)
    : m_state(state)
    , m_parts(std::move(parts))
{
}

void QcTickCommands::addTo(QcCommandRegistry &registry)
{
    registry.addBuiltIn({.name = u"tick"_s, .category = QcCommandCategory::World, .minArguments = 1, .usage = usage,
                         .description = QT_TRANSLATE_NOOP("QcServerMessages",
                                                          "Freezes the world or steps it, changes the tick rate (1 to 100 per second) or runs "
                                                          "ticks as fast as possible; players keep moving."),
                         .run = [this](const QcCommandContext &context) { run(context); }});
}

void QcTickCommands::run(const QcCommandContext &context)
{
    const QString &what = context.args[0];
    if (what == u"query"_s && context.args.size() == 1) {
        query(context);
    } else if ((what == u"freeze"_s || what == u"unfreeze"_s) && context.args.size() == 1) {
        freeze(context, what == u"freeze"_s);
    } else if (what == u"step"_s && context.args.size() <= 2) {
        step(context);
    } else if (what == u"rate"_s && context.args.size() == 2) {
        rate(context);
    } else if (what == u"sprint"_s && context.args.size() == 2) {
        sprint(context);
    } else {
        context.reply(usage);
    }
}

void QcTickCommands::freeze(const QcCommandContext &context, bool frozen)
{
    // A frozen world does not sprint: freezing ends a running sprint first.
    if (frozen && m_parts.ticks.sprinting()) {
        m_parts.ticks.stopSprint();
        reportSprintEnd();
    }

    m_parts.control.setFrozen(frozen);
    m_parts.timeChanged();
    qCInfo(lcCommands).noquote() << (frozen ? u"World frozen by %1"_s : u"World unfrozen by %1"_s).arg(context.source.name());
    context.reply(frozen ? QT_TRANSLATE_NOOP("QcServerMessages", "The world is frozen; /tick step moves it on")
                         : QT_TRANSLATE_NOOP("QcServerMessages", "The world runs again"));
}

void QcTickCommands::step(const QcCommandContext &context)
{
    bool ok = true;
    const quint64 ticks = context.args.size() == 2 ? context.args[1].toULongLong(&ok) : 1;
    if (!m_parts.control.frozen()) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Steps only work while the world is frozen (/tick freeze)"));
        return;
    }
    if (!ok || ticks < 1 || ticks > QcTickControl::maxSteps) {
        context.reply(usage);
        return;
    }

    m_parts.control.step(ticks);
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Stepping %1 ticks"), {QString::number(ticks)});
}

void QcTickCommands::rate(const QcCommandContext &context)
{
    bool ok = true;
    const QString &value = context.args[1];
    const double wanted = value == u"reset"_s ? QcTickScheduler::defaultRate : value.toDouble(&ok);
    if (!ok || !std::isfinite(wanted) || wanted < QcTickScheduler::minRate || wanted > QcTickScheduler::maxRate) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "The tick rate must lie between %1 and %2 ticks per second"),
                      {number(QcTickScheduler::minRate), number(QcTickScheduler::maxRate)});
        return;
    }

    m_parts.ticks.setRate(wanted);
    m_parts.timeChanged();
    qCInfo(lcCommands).noquote() << u"Tick rate set to %1 by %2"_s.arg(number(m_parts.ticks.rate()), context.source.name());
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "The tick rate is now %1 per second"), {number(m_parts.ticks.rate())});
}

std::optional<quint64> QcTickCommands::parseDuration(const QString &word)
{
    const std::array<std::pair<QStringView, double>, 5> units{{{u"min", 60.0}, {u"s", 1.0}, {u"h", 3600.0}, {u"d", QcDayCycle::dayLength},
                                                               {u"t", QcPhysics::tickDuration}}};
    double seconds = 0.0;
    bool ok = false;
    const auto unit = std::ranges::find_if(units, [&word](const auto &entry) { return word.endsWith(entry.first); });
    if (unit != units.end()) {
        seconds = QStringView(word).chopped(unit->first.size()).toDouble(&ok) * unit->second;
    } else {
        seconds = word.toDouble(&ok) * QcPhysics::tickDuration;
    }

    const double ticks = std::round(seconds * QcPhysics::tickRate);
    if (!ok || !std::isfinite(ticks) || ticks < 1.0 || ticks > static_cast<double>(maxSprintTicks)) {
        return std::nullopt;
    }
    return static_cast<quint64>(ticks);
}

void QcTickCommands::sprint(const QcCommandContext &context)
{
    if (context.args[1] == u"stop"_s) {
        if (!m_parts.ticks.sprinting()) {
            context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "No sprint is running"));
            return;
        }
        m_parts.ticks.stopSprint();
        reportSprintEnd();
        return;
    }

    const std::optional<quint64> ticks = parseDuration(context.args[1]);
    if (m_parts.control.frozen()) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "The world is frozen; unfreeze it first or use /tick step"));
        return;
    }
    if (!ticks) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "A sprint runs 1 to %1 ticks"), {QString::number(maxSprintTicks)});
        return;
    }

    m_parts.ticks.startSprint(*ticks);
    qCInfo(lcCommands).noquote() << u"Sprint of %1 ticks started by %2"_s.arg(*ticks).arg(context.source.name());
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Sprinting %1 ticks (%2 s of game time); /tick sprint stop ends it"),
                  {QString::number(*ticks), number(static_cast<double>(*ticks) * QcPhysics::tickDuration)});
}

void QcTickCommands::reportSprintEnd()
{
    const std::optional<QcTickScheduler::SprintEnd> sprint = m_parts.ticks.takeSprintEnded();
    if (!sprint) {
        return;
    }

    const quint64 ticks = sprint->ticks;
    const double seconds = std::max(static_cast<double>(sprint->elapsedNs) / 1e9, 1e-9);
    const QStringList arguments{QString::number(ticks), number(seconds * 1000.0), number(static_cast<double>(ticks) / seconds)};
    qCInfo(lcCommands).noquote() << u"Sprint ended: %1 ticks in %2 ms (%3 ticks per second)"_s.arg(arguments[0], arguments[1], arguments[2]);
    for (const auto &client : m_state.clients()) {
        if (client->state == QcServerClient::State::Playing && m_state.permissionOf(*client) != QcPermission::Player) {
            client->connection->send(
                QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "Sprint ended: %1 ticks in %2 ms (%3 ticks per second)"), arguments));
        }
    }
    m_parts.timeChanged();
}

void QcTickCommands::query(const QcCommandContext &context) const
{
    const QcTickScheduler &ticks = m_parts.ticks;
    const QcPhaseWindow &window = m_parts.phases.window();
    const QStringList rates{number(ticks.rate()), number(ticks.measuredRate()), QString::number(m_parts.control.pendingSteps())};
    context.reply(m_parts.control.frozen() ? QT_TRANSLATE_NOOP("QcServerMessages", "Tick rate %1 per second (measured %2), frozen, steps left: %3")
                                           : QT_TRANSLATE_NOOP("QcServerMessages", "Tick rate %1 per second (measured %2), running"),
                  rates);
    if (ticks.sprinting()) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Sprint: %1 ticks done, %2 to go"),
                      {QString::number(ticks.sprintDone()), QString::number(ticks.sprintRemaining())});
    }

    QStringList phases;
    for (int phase = 0; phase < QcTickPhases::count; ++phase) {
        phases.append(u"%1 %2"_s.arg(QString::fromLatin1(QcTickPhases::ids[static_cast<std::size_t>(phase)]), QString::number(window.mean(phase), 'f', 2)));
    }
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Tick %1 ms mean, %2 ms max; phases (ms): %3"),
                  {QString::number(window.mean(QcTickPhaseTimer::durationSeries), 'f', 2),
                   QString::number(window.max(QcTickPhaseTimer::durationSeries), 'f', 2), phases.join(u", "_s)});
}
