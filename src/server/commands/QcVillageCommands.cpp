#include "QcVillageCommands.h"

#include "QcCommandArguments.h"
#include "QcServerState.h"
#include "QcVillagePlan.h"

#include <QJsonDocument>

#include <cmath>

using namespace Qt::StringLiterals;

QcVillageCommands::QcVillageCommands(QcServerState &state)
    : m_state(state)
{
}

void QcVillageCommands::addTo(QcCommandRegistry &registry)
{
    registry.addBuiltIn({.name = u"locate"_s, .needsPlayer = true, .minArguments = 1,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /locate village"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Finds the nearest village."),
                         .run = [this](const QcCommandContext &context) { locate(context); }});
    registry.addBuiltIn({.name = u"village"_s,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /village [<x> <z>]"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Shows the state of the nearest village as JSON."),
                         .run = [this](const QcCommandContext &context) { village(context); }});
}

std::optional<QcVillageCommands::Found> QcVillageCommands::nearest(QcBlockPos from) const
{
    std::shared_ptr<const QcVillagePlan> plan = m_state.world().generator().nearestVillage(from, searchRadius);
    if (!plan) {
        return std::nullopt;
    }
    const QcVillages &villages = m_state.world().villages();
    if (const QcVillage *known = villages.find(plan->id)) {
        return Found{*known, true, std::move(plan)};
    }
    QcVillage planned = villages.fromPlan(*plan);
    return Found{std::move(planned), false, std::move(plan)};
}

void QcVillageCommands::locate(const QcCommandContext &context)
{
    const std::optional<QcBlockPos> origin = context.source.origin();
    if (context.args.size() != 1 || context.args[0] != u"village"_s || !origin) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /locate village"));
        return;
    }

    const std::optional<Found> found = nearest(*origin);
    if (!found) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "No village within %1 blocks"), {QString::number(searchRadius)});
        return;
    }
    const QcBlockPos centre = found->village.centre;
    const int distance = int(std::lround(std::hypot(double(centre.x - origin->x), double(centre.z - origin->z))));
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Village (%1, %2) at %3 %4 %5, %6 blocks away"),
                  {found->village.style, found->plan->layout, QString::number(centre.x), QString::number(centre.y), QString::number(centre.z),
                   QString::number(distance)});
}

void QcVillageCommands::village(const QcCommandContext &context)
{
    std::optional<QcBlockPos> from = context.source.origin();
    if (context.args.size() == 2) {
        const std::optional<int> x = QcCommandArguments::blockCoordinate(context.args[0], from ? std::optional<int>(from->x) : std::nullopt);
        const std::optional<int> z = QcCommandArguments::blockCoordinate(context.args[1], from ? std::optional<int>(from->z) : std::nullopt);
        from = x && z ? std::optional<QcBlockPos>(QcBlockPos{*x, 0, *z}) : std::nullopt;
    } else if (!context.args.isEmpty()) {
        from.reset();
    }
    if (!from) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /village [<x> <z>]"));
        return;
    }

    const std::optional<Found> found = nearest(*from);
    if (!found) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "No village within %1 blocks"), {QString::number(searchRadius)});
        return;
    }
    QJsonObject json = found->village.toJson();
    json.insert(u"known"_s, found->known);
    json.insert(u"freeBeds"_s, qint64(found->village.freeBeds()));
    // The street plan (decision E-408) comes from the plan; the state does not keep it.
    json.insert(u"layout"_s, found->plan->layout);
    json.insert(u"winding"_s, found->plan->winding);
    context.source.replyPlain(QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
}
