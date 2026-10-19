#include "server/commands/QcVillageCommands.h"

#include "common/content/registry/QcContentNames.h"
#include "server/commands/QcCommandArguments.h"
#include "server/core/QcServerState.h"
#include "server/villages/QcVillagerBehaviour.h"
#include "worldgen/villages/QcVillagePlan.h"

#include <QJsonDocument>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

QcVillageCommands::QcVillageCommands(QcServerState &state)
    : m_state(state)
{
}

void QcVillageCommands::addTo(QcCommandRegistry &registry)
{
    registry.addBuiltIn({.name = u"locate"_s, .needsPlayer = true, .minArguments = 1,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /locate village|<structure>"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Finds the nearest village or surface structure."),
                         .run = [this](const QcCommandContext &context) { locate(context); }});
    registry.addBuiltIn({.name = u"village"_s,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /village [<x> <z>]"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Shows the state of the nearest village as JSON."),
                         .run = [this](const QcCommandContext &context) { village(context); }});
    registry.addBuiltIn({.name = u"profession"_s, .needsPlayer = true, .minArguments = 1,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /profession <profession> [level]"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Gives the nearest villager a profession and trading level."),
                         .run = [this](const QcCommandContext &context) { profession(context); }});
}

void QcVillageCommands::profession(const QcCommandContext &context)
{
    const QString name = context.args[0].contains(u':') ? context.args[0] : u"qtcraft:"_s + context.args[0];
    bool numeric = context.args.size() < 2;
    const int level = context.args.size() < 2 ? 1 : context.args[1].toInt(&numeric);
    if (!m_state.content().profession(name) || !numeric || level < 1 || level > QcVillagerRecord::maxLevel || context.args.size() > 2) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /profession <profession> [level]"));
        return;
    }

    QcCreature *nearest = nullptr;
    double best = professionReach;
    for (QcCreature &mob : m_state.entities().creatures().editableMobs()) {
        const double distance = (mob.motion.position - context.player().motion.position).length();
        if (mob.isVillager() && distance <= best) {
            nearest = &mob;
            best = distance;
        }
    }
    if (!nearest) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "No villager within %1 blocks"), {QString::number(professionReach)});
        return;
    }

    // Experience at the level's start (at least 1: a villager with experience keeps its profession
    // without a workstation), so it levels on from there.
    const QcTradingSettings &trading = m_state.content().villagerSettingsOf(nearest->type).trading;
    nearest->villager->profession = name;
    nearest->villager->level = level;
    nearest->villager->experience = std::max(1, trading.levels.value(level - 1));
    // Its offers follow at once; an open trading screen shows them with the next tick.
    nearest->villager->offers.clear();
    m_state.entities().creatures().villagers().trades(nearest->type).sync(*nearest->villager);
    nearest->villager->lookChanged = true;
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Villager %1 is now %2 of level %3"),
                  {QString::number(nearest->id), name, QString::number(level)});
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
    if (context.args.size() != 1 || !origin) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /locate village|<structure>"));
        return;
    }
    if (context.args[0] != u"village"_s) {
        locateStructure(context, *origin);
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

void QcVillageCommands::locateStructure(const QcCommandContext &context, QcBlockPos origin)
{
    // A structure the generator places by itself (decision E-590): its floor's middle.
    const QString name = QcContentNames::qualify(context.args[0], u"qtcraft"_s);
    const std::optional<QcStructurePlacement> found = m_state.world().generator().nearestStructure(name, origin, searchRadius);
    if (!found) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "No structure %1 within %2 blocks"), {name, QString::number(searchRadius)});
        return;
    }
    const QcBlockPos middle = found->toWorld(QcBlockPos{found->structure().sizeX() / 2, found->structure().groundLayer + 1, found->structure().sizeZ() / 2});
    const int distance = int(std::lround(std::hypot(double(middle.x - origin.x), double(middle.z - origin.z))));
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "%1 at %2 %3 %4, %5 blocks away"),
                  {name, QString::number(middle.x), QString::number(middle.y), QString::number(middle.z), QString::number(distance)});
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
