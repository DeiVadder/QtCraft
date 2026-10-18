#include "server/commands/QcMobCommands.h"

#include "common/physics/QcPhysics.h"
#include "common/physics/QcRaycast.h"
#include "server/commands/QcCommandArguments.h"
#include "server/core/QcServerClient.h"
#include "server/core/QcServerState.h"

#include <QtMath>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr const char *usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /mob <nearest|looking|id> noai <on|off>");

} // namespace

QcMobCommands::QcMobCommands(QcServerState &state)
    : m_state(state)
{
}

void QcMobCommands::addTo(QcCommandRegistry &registry)
{
    registry.addBuiltIn({.name = u"mob"_s,
                         .needsPlayer = true,
                         .minArguments = 3,
                         .usage = usage,
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Takes a creature's mind away or gives it back."),
                         .run = [this](const QcCommandContext &context) { mob(context); }});
}

void QcMobCommands::setNoAi(QcServerState &state, QcCreature &mob, bool noAi)
{
    mob.noAi = noAi;
    if (noAi) {
        // Kept for good, no longer only for a block it carries.
        state.entities().creatures().keep(mob, true);
        mob.keptForBlock = false;
    }
    mob.path.clear();
}

void QcMobCommands::mob(const QcCommandContext &context)
{
    const std::optional<bool> on = context.args.size() == 3 ? QcCommandArguments::toggle(context.args[2]) : std::nullopt;
    if (context.args[1] != u"noai"_s || !on) {
        context.reply(usage);
        return;
    }

    QcCreature *creature = target(context.player(), context.args[0]);
    if (!creature) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "No creature found: %1"), {context.args[0]});
        return;
    }
    setNoAi(m_state, *creature, *on);
    const QString name = m_state.content().mobs()[creature->type].name;
    if (*on) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "%1 %2 has no mind now"), {name, QString::number(creature->id)});
    } else {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "%1 %2 has its mind back"), {name, QString::number(creature->id)});
    }
}

QcCreature *QcMobCommands::target(const QcServerClient &client, const QString &word) const
{
    if (word == u"looking"_s) {
        return inView(client);
    }

    bool isId = false;
    const quint32 id = word.toUInt(&isId);
    if (isId) {
        return m_state.entities().creatures().find(id);
    }
    if (word != u"nearest"_s) {
        return nullptr;
    }
    const QcCreature *best = nullptr;
    double nearest = targetRange;
    for (const QcCreature &mob : m_state.entities().creatures().mobs()) {
        const double distance = (mob.motion.position - client.motion.position).length();
        if (!mob.dormant && distance <= nearest) {
            nearest = distance;
            best = &mob;
        }
    }
    return best ? m_state.entities().creatures().find(best->id) : nullptr;
}

QcCreature *QcMobCommands::inView(const QcServerClient &client) const
{
    const double yaw = qDegreesToRadians(double(client.yaw));
    const double pitch = qDegreesToRadians(double(client.pitch));
    const QcVec3 direction{std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
    const QcVec3 eye = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};

    const QcCreature *best = nullptr;
    double nearest = targetRange;
    for (const QcCreature &mob : m_state.entities().creatures().mobs()) {
        const std::optional<QcAabb> box = mob.dormant ? std::nullopt : m_state.entities().boxOf(mob);
        const std::optional<double> distance = box ? QcRaycast::intersectBox(eye, direction, *box, nearest) : std::nullopt;
        if (distance && *distance < nearest) {
            nearest = *distance;
            best = &mob;
        }
    }
    return best ? m_state.entities().creatures().find(best->id) : nullptr;
}
