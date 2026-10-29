#include "server/commands/QcAdvancementCommands.h"

#include "server/commands/QcCommandArguments.h"
#include "server/core/QcServerClient.h"
#include "server/core/QcServerState.h"

using namespace Qt::StringLiterals;

namespace {

constexpr const char *usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /advancement <grant|revoke|query> <player> [<advancement>|everything]");

} // namespace

QcAdvancementCommands::QcAdvancementCommands(QcServerState &state)
    : m_state(state)
{
}

void QcAdvancementCommands::addTo(QcCommandRegistry &registry)
{
    registry.addBuiltIn({.name = u"advancement"_s, .category = QcCommandCategory::Players, .aliases = {u"advancements"_s},
                         .minArguments = 2,
                         .usage = usage,
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Grants, revokes or shows the advancements of a player."),
                         .run = [this](const QcCommandContext &context) { advancement(context); },
                         .names = [this] {
                             QStringList names{u"everything"_s};
                             for (const QcAdvancementDefinition &definition : m_state.content().advancements().items()) {
                                 names.append(definition.name);
                             }
                             return names;
                         },
                         .namesLine = QT_TRANSLATE_NOOP("QcServerMessages", "Advancements: %1")});
}

void QcAdvancementCommands::advancement(const QcCommandContext &context)
{
    const QStringList &args = context.args;
    const QString &action = args[0];
    const bool changing = action == u"grant"_s || action == u"revoke"_s;
    if ((!changing && action != u"query"_s) || (changing && args.size() != 3) || args.size() > 3) {
        context.reply(usage);
        return;
    }

    const QList<QcServerClient *> targets = QcCommandArguments::players(m_state, args[1]);
    if (targets.isEmpty()) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "No player named %1 is online"), {args[1]});
        return;
    }
    const QString target = args.size() > 2 ? args[2] : QString();
    for (QcServerClient *player : targets) {
        if (!changing) {
            query(context, *player, target);
            continue;
        }
        const std::optional<int> changed = change(*player, target, action == u"grant"_s);
        if (!changed) {
            context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Unknown advancement: %1"), {target});
            return;
        }
        // Chat lines of the server carry no plural forms; the number stands where it reads right
        // with any count.
        context.reply(action == u"grant"_s ? QT_TRANSLATE_NOOP("QcServerMessages", "Advancements granted to %2: %1")
                                           : QT_TRANSLATE_NOOP("QcServerMessages", "Advancements revoked from %2: %1"),
                      {QString::number(*changed), player->name});
    }
}

std::optional<int> QcAdvancementCommands::change(QcServerClient &player, const QString &target, bool grant) const
{
    const auto apply = [&](const QString &name) {
        return grant ? m_state.advancements().grant(player, name) : m_state.advancements().revoke(player, name);
    };
    if (target == u"everything"_s) {
        int changed = 0;
        for (const QcAdvancementDefinition &definition : m_state.content().advancements().items()) {
            changed += apply(definition.name) ? 1 : 0;
        }
        return changed;
    }
    const QString name = QcCommandArguments::qualified(target);
    if (!m_state.content().advancements().find(name)) {
        return std::nullopt;
    }
    return apply(name) ? 1 : 0;
}

void QcAdvancementCommands::query(const QcCommandContext &context, const QcServerClient &player, const QString &target) const
{
    const QcAdvancementProgress &progress = player.advancements.progress;
    if (target.isEmpty()) {
        QStringList reached;
        for (const QcAdvancementDefinition &definition : m_state.content().advancements().items()) {
            if (progress.isDone(definition)) {
                reached.append(definition.name);
            }
        }
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "%1 has reached %2 of %3 advancements: %4"),
                      {player.name, QString::number(reached.size()), QString::number(m_state.content().advancements().items().size()),
                       reached.join(u", "_s)});
        return;
    }

    const QcAdvancementDefinition *definition = m_state.content().advancements().find(QcCommandArguments::qualified(target));
    if (!definition) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Unknown advancement: %1"), {target});
        return;
    }
    QStringList met = progress.criteria(definition->name).values();
    met.sort();
    context.reply(progress.isDone(*definition) ? QT_TRANSLATE_NOOP("QcServerMessages", "%1 has reached %2 (criteria met: %3)")
                                               : QT_TRANSLATE_NOOP("QcServerMessages", "%1 has not reached %2 yet (criteria met: %3)"),
                  {player.name, definition->name, met.join(u", "_s)});
}
