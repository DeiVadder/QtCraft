#include "server/commands/QcEffectCommands.h"

#include "common/content/definitions/QcEffectDefinition.h"
#include "common/physics/QcPhysics.h"
#include "server/commands/QcCommandArguments.h"
#include "server/core/QcServerClient.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcServerEffects.h"

using namespace Qt::StringLiterals;

namespace {

constexpr const char *usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /effect give <player> <effect> [seconds] [level] or /effect clear <player> [effect]");

// The longest effect /effect gives, in seconds (QcStatusEffects::maxTicks).
constexpr int maxSeconds = QcStatusEffects::maxTicks / static_cast<int>(QcPhysics::tickRate);

} // namespace

QcEffectCommands::QcEffectCommands(QcServerState &state)
    : m_state(state)
{
}

void QcEffectCommands::addTo(QcCommandRegistry &registry)
{
    registry.addBuiltIn({.name = u"effect"_s, .category = QcCommandCategory::Items,
                         .minArguments = 2,
                         .usage = usage,
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Gives a player a status effect or takes effects away."),
                         .run = [this](const QcCommandContext &context) { effect(context); }});
}

void QcEffectCommands::effect(const QcCommandContext &context)
{
    const QString &action = context.args[0];
    if ((action != u"give"_s && action != u"clear"_s) || (action == u"give"_s && context.args.size() < 3) || context.args.size() > 5) {
        context.reply(usage);
        return;
    }

    const QList<QcServerClient *> targets = QcCommandArguments::players(m_state, context.args[1]);
    if (targets.isEmpty()) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "No player named %1 is online"), {context.args[1]});
        return;
    }
    if (action == u"give"_s) {
        give(context, targets);
    } else {
        clear(context, targets);
    }
}

void QcEffectCommands::give(const QcCommandContext &context, const QList<QcServerClient *> &targets)
{
    const QString name = QcCommandArguments::qualified(context.args[2]);
    const std::optional<int> seconds = QcCommandArguments::count(context.args, 3, defaultSeconds, 1, maxSeconds);
    const std::optional<int> level = QcCommandArguments::count(context.args, 4, 1, 1, QcEffectDefinition::maxLevel);
    if (!m_state.content().effect(name)) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Unknown effect: %1"), {context.args[2]});
        return;
    }
    if (!seconds || !level) {
        context.reply(usage);
        return;
    }

    for (QcServerClient *player : targets) {
        const QcStatusEffect effect{name, *level, *seconds * static_cast<int>(QcPhysics::tickRate)};
        if (QcServerEffects::givePlayer(m_state.content(), *player, effect)) {
            context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Gave %1 level %2 for %3 s to %4"),
                          {name, QString::number(*level), QString::number(*seconds), player->name});
        } else {
            context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "%1 already has a stronger or longer %2"), {player->name, name});
        }
    }
}

void QcEffectCommands::clear(const QcCommandContext &context, const QList<QcServerClient *> &targets) const
{
    const QString name = context.args.size() > 2 ? QcCommandArguments::qualified(context.args[2]) : QString();
    for (QcServerClient *player : targets) {
        const bool took = name.isEmpty() ? player->effects.clear() : player->effects.remove(name);
        if (took) {
            context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Took the effects away from %1"), {player->name});
        } else {
            context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "%1 has no such effect"), {player->name});
        }
    }
}
