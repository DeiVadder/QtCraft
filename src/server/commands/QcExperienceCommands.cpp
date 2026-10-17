#include "server/commands/QcExperienceCommands.h"

#include "server/commands/QcCommandArguments.h"
#include "server/core/QcServerClient.h"
#include "server/core/QcServerState.h"

#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace {

constexpr const char *usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /xp <add|set|query> <player> [amount] [points|levels]");

// The most /xp adds or sets at once: every level there is, or its points.
constexpr int maxAmount = 500'000'000;

} // namespace

QcExperienceCommands::QcExperienceCommands(QcServerState &state)
    : m_state(state)
{
}

void QcExperienceCommands::addTo(QcCommandRegistry &registry)
{
    registry.addBuiltIn({.name = u"xp"_s, .aliases = {u"experience"_s},
                         .minArguments = 2,
                         .usage = usage,
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Adds to, sets or shows the experience of a player, in points or levels."),
                         .run = [this](const QcCommandContext &context) { xp(context); }});
}

void QcExperienceCommands::xp(const QcCommandContext &context)
{
    const QStringList &args = context.args;
    const QString &action = args[0];
    const bool asking = action == u"query"_s;
    const qsizetype unitIndex = asking ? 2 : 3;
    const QString unit = args.size() > unitIndex ? args[unitIndex] : u"points"_s;
    const bool levels = unit == u"levels"_s;
    const bool amountNeeded = action == u"add"_s || action == u"set"_s;
    const std::optional<int> amount = amountNeeded ? QcCommandArguments::count(args, 2, std::numeric_limits<int>::min(), -maxAmount, maxAmount)
                                                   : std::optional<int>(0);
    const bool known = asking || amountNeeded;
    if (!known || args.size() > unitIndex + 1 || (unit != u"points"_s && !levels) || !amount || *amount == std::numeric_limits<int>::min()) {
        context.reply(usage);
        return;
    }

    const QList<QcServerClient *> targets = QcCommandArguments::players(m_state, args[1]);
    if (targets.isEmpty()) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "No player named %1 is online"), {args[1]});
        return;
    }

    for (QcServerClient *player : targets) {
        if (asking) {
            query(context, *player);
        } else if (action == u"add"_s) {
            add(context, *player, *amount, levels);
        } else {
            set(context, *player, *amount, levels);
        }
    }
}

void QcExperienceCommands::add(const QcCommandContext &context, QcServerClient &player, int amount, bool levels) const
{
    if (levels) {
        player.experience.addLevels(amount);
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Gave %1 experience levels to %2"), {QString::number(amount), player.name});
    } else {
        player.experience.addPoints(amount);
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Gave %1 experience points to %2"), {QString::number(amount), player.name});
    }
    query(context, player);
}

void QcExperienceCommands::set(const QcCommandContext &context, QcServerClient &player, int amount, bool levels) const
{
    if (amount < 0) {
        context.reply(usage);
        return;
    }

    if (levels) {
        player.experience.setLevel(amount);
    } else if (!player.experience.setPoints(amount)) {
        // As in the reference: points are set within the current level.
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Level %1 of %2 holds at most %3 points; use levels or /xp add"),
                      {QString::number(player.experience.level()), player.name, QString::number(QcExperience::mostPoints(player.experience.level()))});
        return;
    }
    query(context, player);
}

void QcExperienceCommands::query(const QcCommandContext &context, const QcServerClient &player)
{
    const QcExperience &experience = player.experience;
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "%1 has experience level %2 and %3 of %4 points towards the next (%5 points in all)"),
                  {player.name, QString::number(experience.level()), QString::number(experience.points()),
                   QString::number(QcExperience::pointsForNext(experience.level())), QString::number(experience.total())});
}
