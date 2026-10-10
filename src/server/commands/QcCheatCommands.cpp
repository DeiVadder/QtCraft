#include "QcCheatCommands.h"

#include "QcCommandArguments.h"
#include "QcDayCycle.h"
#include "QcPhysics.h"
#include "QcServerCombat.h"
#include "QcServerInventory.h"
#include "QcServerState.h"

#include <QtMath>

#include <array>
#include <cmath>
#include <utility>

using namespace Qt::StringLiterals;
using QcCommandArguments::qualified;

namespace {

// The words as QcStandCommands reads them: the command first.
QStringList withName(const QString &name, const QStringList &args)
{
    return QStringList{name} + args;
}

} // namespace

QcCheatCommands::QcCheatCommands(QcServerState &state, Parts parts)
    : m_state(state)
    , m_parts(std::move(parts))
    , m_standCommands(state, m_parts.stands, m_parts.inventory)
{
}

void QcCheatCommands::addTo(QcCommandRegistry &registry)
{
    addItemCommands(registry);
    addPlayerCommands(registry);
    addWorldCommands(registry);
}

void QcCheatCommands::addItemCommands(QcCommandRegistry &registry)
{
    registry.addBuiltIn({.name = u"give"_s, .needsPlayer = true, .minArguments = 1,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /give <item> [count]"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Puts items into your inventory."),
                         .run = [this](const QcCommandContext &context) { give(context); }});
    registry.addBuiltIn({.name = u"spawnitem"_s, .needsPlayer = true, .minArguments = 1,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /spawnitem <item> [count]"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Drops up to 64 items two metres ahead of you."),
                         .run = [this](const QcCommandContext &context) { spawnItem(context); }});
    registry.addBuiltIn({.name = u"summon"_s, .needsPlayer = true, .minArguments = 1,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /summon <creature> [count] or /summon armor_stand [property=value …]"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Summons creatures or an armour stand in front of you."),
                         .run = [this](const QcCommandContext &context) { summon(context); }});
    registry.addBuiltIn({.name = u"equip"_s, .needsPlayer = true, .minArguments = 1,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /equip <armour|set|clear>"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Puts on armour; replaced pieces go into your inventory."),
                         .run = [this](const QcCommandContext &context) {
                             m_standCommands.equip(context.player(), withName(u"/equip"_s, context.args));
                         }});
    registry.addBuiltIn({.name = u"armorstand"_s, .needsPlayer = true,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /armorstand <nearest|looking|id> <property> [values]"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Poses and dresses an armour stand; 'info' lists its properties."),
                         .run = [this](const QcCommandContext &context) {
                             m_standCommands.armorStand(context.player(), withName(u"/armorstand"_s, context.args));
                         }});
}

void QcCheatCommands::addPlayerCommands(QcCommandRegistry &registry)
{
    registry.addBuiltIn({.name = u"kill"_s, .needsPlayer = true,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /kill"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Kills you."),
                         .run = [this](const QcCommandContext &context) { kill(context); }});
    registry.addBuiltIn({.name = u"heal"_s, .needsPlayer = true,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /heal"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Restores your health."),
                         .run = [this](const QcCommandContext &context) { heal(context); }});
    registry.addBuiltIn({.name = u"feed"_s, .needsPlayer = true,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /feed [food points]"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Sets your food points (full without a number)."),
                         .run = [this](const QcCommandContext &context) { feed(context); }});
    registry.addBuiltIn({.name = u"gamemode"_s,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /gamemode [survival] [player]"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Shows or sets the game mode; so far there is only survival."),
                         .run = [this](const QcCommandContext &context) { gamemode(context); }});
}

void QcCheatCommands::addWorldCommands(QcCommandRegistry &registry)
{
    registry.addBuiltIn({.name = u"time"_s,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /time set <day|night|seconds> or /time add <seconds>"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Shows or changes the time of day (a day lasts 600 s)."),
                         .run = [this](const QcCommandContext &context) { time(context); }});
    registry.addBuiltIn({.name = u"gamerule"_s, .permission = QcPermission::Player, .minArguments = 1,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /gamerule <keepInventory|daylightCycle|mobSpawning> [true|false]"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Shows a game rule; operators change it."),
                         .run = [this](const QcCommandContext &context) { gamerule(context); }});
    registry.addBuiltIn({.name = u"locatebiome"_s, .permission = QcPermission::Player, .needsPlayer = true, .minArguments = 1,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /locatebiome <biome>"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Finds the nearest place of a biome."),
                         .run = [this](const QcCommandContext &context) { locateBiome(context); }});
}

void QcCheatCommands::give(const QcCommandContext &context)
{
    const QString name = qualified(context.args[0]);
    const std::optional<QcItemId> item = m_state.content->itemId(name);
    const std::optional<int> count = QcCommandArguments::count(context.args, 1, 1, 1, 64 * QcInventory::slotCount);
    if (!item || !count) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Unknown item or invalid count: %1"), {context.text});
        return;
    }

    QcServerClient &player = context.player();
    const int leftover = player.inventory.add(*item, *count, *m_state.content);
    m_parts.inventory.send(player);
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Gave %1 x %2"), {QString::number(*count - leftover), name});
}

void QcCheatCommands::spawnItem(const QcCommandContext &context)
{
    const QString name = qualified(context.args[0]);
    const std::optional<QcItemId> item = m_state.content->itemId(name);
    const std::optional<int> count = QcCommandArguments::count(context.args, 1, 1, 1, 64);
    if (!item || !count) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Unknown item or invalid count: %1"), {context.text});
        return;
    }

    // Two metres in front of the player, on the ground.
    const QcServerClient &player = context.player();
    const double yaw = qDegreesToRadians(double(player.yaw));
    const QcVec3 position = player.motion.position + QcVec3{std::sin(yaw) * 2.0, 0.5, -std::cos(yaw) * 2.0};
    m_state.items.spawnScattered({*item, *count, 0}, position, m_state.newEntityId());
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Dropped %1 x %2"), {QString::number(*count), name});
}

void QcCheatCommands::summon(const QcCommandContext &context)
{
    QcServerClient &player = context.player();
    if (m_standCommands.summon(player, withName(u"/summon"_s, context.args))) {
        return;
    }

    const QString name = qualified(context.args[0]);
    const std::optional<int> type = m_state.content->mobIndex(name);
    const std::optional<int> count = QcCommandArguments::count(context.args, 1, 1, 1, 16);
    if (!type || !count) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Unknown mob or invalid count: %1"), {context.text});
        return;
    }

    if (m_state.mobs.awakeCount() + *count > QcMobManager::summonLimit) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Too many creatures in the world (at most %1)"),
                      {QString::number(QcMobManager::summonLimit)});
        return;
    }

    // Three metres in front of the player.
    const double yaw = qDegreesToRadians(double(player.yaw));
    const QcVec3 position = player.motion.position + QcVec3{std::sin(yaw) * 3.0, 0.5, -std::cos(yaw) * 3.0};
    for (int i = 0; i < *count; ++i) {
        const int row = i / 3; // NOLINT(bugprone-integer-division): grid row
        // Summoned animals stay like natural ones.
        const QcMobDefinition &summoned = m_state.content->mobs()[*type];
        m_state.mobs.spawn(*type, position + QcVec3{(i % 3) * 0.8, 0.0, row * 0.8}, m_state.newEntityId(), false,
                           !summoned.hostile && !summoned.ambient && !summoned.flying);
    }

    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Summoned %1 x %2"), {QString::number(*count), name});
}

void QcCheatCommands::kill(const QcCommandContext &context)
{
    QcServerClient &player = context.player();
    player.hurtTimer = 0.0;
    m_parts.combat.damagePlayer(player, player.health, {});
}

void QcCheatCommands::feed(const QcCommandContext &context)
{
    QcServerClient &player = context.player();
    if (player.dead) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Respawn first"));
        return;
    }

    const std::optional<int> food = QcCommandArguments::count(context.args, 0, QcHunger::maxFood, 0, QcHunger::maxFood);
    if (!food) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /feed [0..%1]"), {QString::number(QcHunger::maxFood)});
        return;
    }

    player.hunger.restore(*food, 0.0);
    m_parts.combat.sendFood(player);
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Food is now %1"), {QString::number(*food)});
}

void QcCheatCommands::heal(const QcCommandContext &context)
{
    QcServerClient &player = context.player();
    if (player.dead) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Respawn first"));
        return;
    }

    player.health = QcServerClient::maxHealth;
    m_parts.combat.sendHealth(player);
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Healed"));
}

void QcCheatCommands::time(const QcCommandContext &context) const
{
    const QStringList &args = context.args;
    const double now = m_state.info.worldTime;
    if (args.isEmpty()) {
        context.reply(QcDayCycle::isNight(now) ? QT_TRANSLATE_NOOP("QcServerMessages", "Time is now %1 s (night)")
                                               : QT_TRANSLATE_NOOP("QcServerMessages", "Time is now %1 s (day)"),
                      {QString::number(qRound(now))});
        return;
    }

    bool ok = args.size() >= 2;
    double seconds = 0.0;
    const double dayStart = std::floor(now / QcDayCycle::dayLength) * QcDayCycle::dayLength;
    const QString value = args.value(1);
    if (args[0] == u"set"_s && value == u"day"_s) {
        seconds = dayStart + QcDayCycle::dayLength * 1.05;
    } else if (args[0] == u"set"_s && value == u"night"_s) {
        seconds = dayStart + QcDayCycle::dayLength * 0.6;
    } else if (args[0] == u"set"_s && ok) {
        seconds = value.toDouble(&ok);
    } else if (args[0] == u"add"_s && ok) {
        seconds = now + value.toDouble(&ok);
    } else {
        ok = false;
    }

    if (!ok || !std::isfinite(seconds) || seconds < 0.0 || seconds > 1e9) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /time set <day|night|seconds> or /time add <seconds>"));
        return;
    }

    m_parts.setWorldTime(seconds);
    context.reply(QcDayCycle::isNight(seconds) ? QT_TRANSLATE_NOOP("QcServerMessages", "Time is now %1 s (night)")
                                               : QT_TRANSLATE_NOOP("QcServerMessages", "Time is now %1 s (day)"),
                  {QString::number(qRound(seconds))});
}

void QcCheatCommands::gamerule(const QcCommandContext &context)
{
    // The rules of the world (keepInventory; daylightCycle and mobSpawning, decision E-221).
    using Rule = std::pair<QStringView, bool QcWorldInfo::*>;
    const std::array<Rule, 3> rules{{{u"keepInventory", &QcWorldInfo::keepInventory},
                                     {u"daylightCycle", &QcWorldInfo::daylightCycle},
                                     {u"mobSpawning", &QcWorldInfo::mobSpawning}}};
    const QStringList &args = context.args;
    const auto rule = std::ranges::find(rules, QStringView(args[0]), &Rule::first);
    const std::optional<bool> wanted = args.size() >= 2 ? QcCommandArguments::toggle(args[1]) : std::nullopt;
    if (rule == rules.end() || (args.size() >= 2 && !wanted)) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /gamerule <keepInventory|daylightCycle|mobSpawning> [true|false]"));
        return;
    }

    // The values are part of the keys, so they are translated with the line.
    bool &value = m_state.info.*(rule->second);
    if (!wanted) {
        context.reply(value ? QT_TRANSLATE_NOOP("QcServerMessages", "%1 is true") : QT_TRANSLATE_NOOP("QcServerMessages", "%1 is false"),
                      {args[0]});
        return;
    }
    if (!context.source.may(QcPermission::Operator)) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Only operators can use /%1"), {u"gamerule"_s});
        return;
    }

    value = *wanted;
    context.reply(value ? QT_TRANSLATE_NOOP("QcServerMessages", "%1 is now true") : QT_TRANSLATE_NOOP("QcServerMessages", "%1 is now false"),
                  {args[0]});
    if (rule->second == &QcWorldInfo::daylightCycle) {
        m_parts.setWorldTime(m_state.info.worldTime);
    }
}

void QcCheatCommands::gamemode(const QcCommandContext &context)
{
    // Only survival exists until the creative mode (stage 26) adds a mode per player.
    const QStringList &args = context.args;
    const QString target = args.size() >= 2 ? args[1] : context.source.name();
    if (args.size() >= 2 && QcCommandArguments::players(m_state, target).isEmpty()) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "No player named %1 is online"), {target});
        return;
    }

    if (!args.isEmpty() && args[0] != u"survival"_s) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Only the survival game mode exists so far; %1 comes with a later version"), {args[0]});
        return;
    }
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Game mode of %1: survival"), {target});
}

void QcCheatCommands::locateBiome(const QcCommandContext &context)
{
    // Nearest column of the biome on growing squares around the player, in steps of 8 blocks.
    const QString name = qualified(context.args[0]);
    const std::optional<QcBlockPos> origin = context.source.origin();
    if (!origin) {
        return;
    }
    const QcBlockPos feet = *origin;

    for (int radius = 0; radius <= biomeSearchRadius; radius += 8) {
        for (int dx = -radius; dx <= radius; dx += 8) {
            if (const std::optional<int> dz = findBiomeAlong(feet, radius, dx, name)) {
                context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "%1 at %2 %3"), {name, QString::number(feet.x + dx), QString::number(feet.z + *dz)});
                return;
            }
        }
    }

    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "No %1 within %2 blocks"), {name, QString::number(biomeSearchRadius)});
}

std::optional<int> QcCheatCommands::findBiomeAlong(QcBlockPos feet, int radius, int dx, const QString &name) const
{
    for (int dz = -radius; dz <= radius; dz += 8) {
        if (std::max(std::abs(dx), std::abs(dz)) == radius && m_state.world.generator().biomeAt(feet.x + dx, feet.z + dz) == name) {
            return dz;
        }
    }
    return std::nullopt;
}
