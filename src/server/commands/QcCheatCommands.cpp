#include "server/commands/QcCheatCommands.h"

#include "common/physics/QcBodyFit.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcChatText.h"
#include "common/world/QcDayCycle.h"
#include "server/commands/QcCommandArguments.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcPlayerHealth.h"
#include "server/gameplay/QcServerGameModes.h"
#include "server/gameplay/QcServerInventory.h"
#include "server/minecarts/QcServerMinecarts.h"

#include <QtMath>

#include <array>
#include <cmath>
#include <utility>

using namespace Qt::StringLiterals;
using QcCommandArguments::qualified;

namespace {

// One message key per mode (decision E-094): the client translates the whole sentence, mode name
// included, as with day and night in /time.
struct ModeKeys
{
    const char *survival;
    const char *creative;

    const char *operator()(QcGameMode mode) const { return mode == QcGameMode::Creative ? creative : survival; }
};

constexpr ModeKeys modeOfPlayer{QT_TRANSLATE_NOOP("QcServerMessages", "Game mode of %1: survival"),
                                QT_TRANSLATE_NOOP("QcServerMessages", "Game mode of %1: creative")};
constexpr ModeKeys modeSet{QT_TRANSLATE_NOOP("QcServerMessages", "Set the game mode of %1 to survival"),
                           QT_TRANSLATE_NOOP("QcServerMessages", "Set the game mode of %1 to creative")};
constexpr ModeKeys modeNow{QT_TRANSLATE_NOOP("QcServerMessages", "Your game mode is now survival"),
                           QT_TRANSLATE_NOOP("QcServerMessages", "Your game mode is now creative")};
constexpr ModeKeys newPlayersStart{QT_TRANSLATE_NOOP("QcServerMessages", "New players start in survival"),
                                   QT_TRANSLATE_NOOP("QcServerMessages", "New players start in creative")};

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
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /summon <creature|minecart|chest_minecart|hopper_minecart> [count] or /summon armor_stand [property=value …]"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Summons creatures, minecarts or an armour stand in front of you."),
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
    // Everyone may ask for their own mode; changing it checks for an operator.
    registry.addBuiltIn({.name = u"gamemode"_s, .permission = QcPermission::Player,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /gamemode [survival|creative] [player]"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Shows your game mode or sets it for you or another player."),
                         .run = [this](const QcCommandContext &context) { gamemode(context); }});
    registry.addBuiltIn({.name = u"defaultgamemode"_s,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /defaultgamemode [survival|creative]"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Shows or sets the game mode of players who join for the first time."),
                         .run = [this](const QcCommandContext &context) { defaultGamemode(context); }});
}

void QcCheatCommands::addWorldCommands(QcCommandRegistry &registry)
{
    registry.addBuiltIn({.name = u"time"_s,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /time set <day|night|seconds> or /time add <seconds>"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Shows or changes the time of day (a day lasts 600 s)."),
                         .run = [this](const QcCommandContext &context) { time(context); }});
    registry.addBuiltIn({.name = u"gamerule"_s, .permission = QcPermission::Player, .minArguments = 1,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /gamerule <keepInventory|daylightCycle|mobSpawning|randomTicks|fireSpread|quasiConnectivity|weatherCycle> [true|false] | spawnChunkRadius [0-16] | maxEntityCramming [0-4096]"),
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
    const std::optional<QcItemId> item = m_state.content().itemId(name);
    const std::optional<int> count = QcCommandArguments::count(context.args, 1, 1, 1, 64 * QcInventory::slotCount);
    if (!item || !count) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Unknown item or invalid count: %1"), {context.text});
        return;
    }

    QcServerClient &player = context.player();
    const int leftover = player.inventory.add(*item, *count, m_state.content());
    m_parts.inventory.send(player);
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Gave %1 x %2"), {QString::number(*count - leftover), name});
}

void QcCheatCommands::spawnItem(const QcCommandContext &context)
{
    const QString name = qualified(context.args[0]);
    const std::optional<QcItemId> item = m_state.content().itemId(name);
    const std::optional<int> count = QcCommandArguments::count(context.args, 1, 1, 1, 64);
    if (!item || !count) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Unknown item or invalid count: %1"), {context.text});
        return;
    }

    // Two metres in front of the player, on the ground.
    const QcServerClient &player = context.player();
    const double yaw = qDegreesToRadians(double(player.yaw));
    const QcVec3 position = player.motion.position + QcVec3{std::sin(yaw) * 2.0, 0.5, -std::cos(yaw) * 2.0};
    m_state.entities().items().spawnScattered({*item, *count, 0}, position, m_state.entities().newId());
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Dropped %1 x %2"), {QString::number(*count), name});
}

void QcCheatCommands::summon(const QcCommandContext &context)
{
    QcServerClient &player = context.player();
    if (m_standCommands.summon(player, withName(u"/summon"_s, context.args)) || summonCarts(context)) {
        return;
    }

    const QString name = qualified(context.args[0]);
    const std::optional<int> type = m_state.content().mobIndex(name);
    const std::optional<int> count = QcCommandArguments::count(context.args, 1, 1, 1, 16);
    if (!type || !count) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Unknown mob or invalid count: %1"), {context.text});
        return;
    }

    // Only creatures that came by /summon count: the animals of the loaded chunks would make the
    // limit depend on the view distance and on how far the world has loaded (decision E-351).
    const qint64 limit = m_state.config().limits.summonLimit;
    if (m_state.entities().creatures().awakeSummonedCount() + *count > limit) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Too many summoned creatures in the world (at most %1)"), {QString::number(limit)});
        return;
    }

    // Three metres in front of the player.
    const double yaw = qDegreesToRadians(double(player.yaw));
    const QcVec3 position = player.motion.position + QcVec3{std::sin(yaw) * 3.0, 0.5, -std::cos(yaw) * 3.0};
    const QcMobDefinition &definition = m_state.content().mobs()[*type];
    for (int i = 0; i < *count; ++i) {
        const int row = i / 3; // NOLINT(bugprone-integer-division): grid row
        const QcVec3 at = summonPlace(player, definition, position + QcVec3{(i % 3) * 0.8, 0.0, row * 0.8});
        // Summoned animals stay like natural ones.
        const quint32 id = m_state.entities().creatures().spawn(*type, at, m_state.entities().newId(), false,
                                              !definition.hostile && !definition.ambient && !definition.flying);
        m_state.entities().creatures().find(id)->summoned = true;
    }

    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Summoned %1 x %2"), {QString::number(*count), name});
}

bool QcCheatCommands::summonCarts(const QcCommandContext &context)
{
    const QString name = qualified(context.args[0]);
    const std::optional<QcItemId> item = m_state.content().itemId(name);
    if (!item || !m_state.content().item(*item).placesCart) {
        return false;
    }

    const std::optional<int> count = QcCommandArguments::count(context.args, 1, 1, 1, 16);
    if (!count) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Unknown mob or invalid count: %1"), {context.text});
        return true;
    }

    // Three metres in front of the player, one behind the other along the view (a row on a rail
    // laid there); each stands on a rail in its cell or the one below, else falls.
    const QcServerClient &player = context.player();
    const double yaw = qDegreesToRadians(double(player.yaw));
    const QcVec3 along{std::sin(yaw), 0.0, -std::cos(yaw)};
    int summoned = 0;
    for (int i = 0; i < *count; ++i) {
        const QcVec3 wanted = player.motion.position + along * (3.0 + i) + QcVec3{0.0, 0.1, 0.0};
        if (!m_parts.minecarts.summon(name, wanted, player.yaw)) {
            context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Too many minecarts in the world (at most %1)"),
                          {QString::number(m_state.entities().minecarts().limit())});
            break;
        }
        ++summoned;
    }

    if (summoned > 0) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Summoned %1 x %2"), {QString::number(summoned), name});
    }
    return true;
}

QcVec3 QcCheatCommands::summonPlace(const QcServerClient &player, const QcMobDefinition &definition, const QcVec3 &wanted) const
{
    // The nearest place where the body is clear of blocks (over a step or a hill, decision E-352),
    // else the player's feet; also there instead of in lava, magma or fire.
    const QcPhysics::BodySize body{definition.width, definition.height};
    const std::optional<QcVec3> clear = QcBodyFit::clearFeet(m_state.world(), wanted, body);
    if (!clear || m_state.contactDamage().in(m_state.world(), QcPhysics::boxAt(*clear, body.width, body.height)) > 0.0) {
        return player.motion.position;
    }
    return *clear;
}

void QcCheatCommands::kill(const QcCommandContext &context)
{
    QcServerClient &player = context.player();
    player.hurtTimer = 0.0;
    m_parts.health.damage(player, player.health, {}, QcPlayerHealth::DamageKind::Command);
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
    m_parts.health.sendFood(player);
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
    m_parts.health.sendHealth(player);
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Healed"));
}

void QcCheatCommands::time(const QcCommandContext &context) const
{
    const QStringList &args = context.args;
    const double now = m_state.info().worldTime;
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
    // The rules of the world (keepInventory; daylightCycle and mobSpawning, decision E-221;
    // randomTicks, decision E-295; quasiConnectivity, decision E-338; fireSpread, decision E-392;
    // weatherCycle, decision E-395; the numbers spawnChunkRadius, decision E-377, and
    // maxEntityCramming, decision E-401).
    using Rule = std::pair<QStringView, bool QcWorldInfo::*>;
    const std::array<Rule, 7> rules{{{u"keepInventory", &QcWorldInfo::keepInventory},
                                     {u"daylightCycle", &QcWorldInfo::daylightCycle},
                                     {u"mobSpawning", &QcWorldInfo::mobSpawning},
                                     {u"randomTicks", &QcWorldInfo::randomTicks},
                                     {u"fireSpread", &QcWorldInfo::fireSpread},
                                     {u"quasiConnectivity", &QcWorldInfo::quasiConnectivity},
                                     {u"weatherCycle", &QcWorldInfo::weatherCycle}}};
    const QStringList &args = context.args;
    if (args[0] == u"spawnChunkRadius"_s) {
        numberRule(context, QcWorldInfo::maxSpawnChunkRadius, &QcWorldInfo::spawnChunkRadius);
        return;
    }
    if (args[0] == u"maxEntityCramming"_s) {
        numberRule(context, QcWorldInfo::maxEntityCrammingLimit, &QcWorldInfo::maxEntityCramming);
        return;
    }
    const auto rule = std::ranges::find(rules, QStringView(args[0]), &Rule::first);
    const std::optional<bool> wanted = args.size() >= 2 ? QcCommandArguments::toggle(args[1]) : std::nullopt;
    if (rule == rules.end() || (args.size() >= 2 && !wanted)) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /gamerule <keepInventory|daylightCycle|mobSpawning|randomTicks|fireSpread|quasiConnectivity|weatherCycle> [true|false] | spawnChunkRadius [0-16] | maxEntityCramming [0-4096]"));
        return;
    }

    // The values are part of the keys, so they are translated with the line.
    bool &value = m_state.info().*(rule->second);
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
        m_parts.setWorldTime(m_state.info().worldTime);
    }
}

void QcCheatCommands::numberRule(const QcCommandContext &context, int highest, int QcWorldInfo::*rule)
{
    // spawnChunkRadius: the chunks around the spawn that stay loaded (decision E-377): 0 none, 1 the
    // spawn chunk, 2 the 3 x 3 around it and so on. maxEntityCramming: creatures in one block cell
    // beyond which they take damage (decision E-401), 0 off.
    const QStringList &args = context.args;
    int &value = m_state.info().*rule;
    if (args.size() == 1) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "%1 is %2"), {args[0], QString::number(value)});
        return;
    }
    const std::optional<int> wanted = QcCommandArguments::count(args, 1, -1, 0, highest);
    if (!wanted || args.size() != 2) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /gamerule <keepInventory|daylightCycle|mobSpawning|randomTicks|fireSpread|quasiConnectivity|weatherCycle> [true|false] | spawnChunkRadius [0-16] | maxEntityCramming [0-4096]"));
        return;
    }
    if (!context.source.may(QcPermission::Operator)) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Only operators can use /%1"), {u"gamerule"_s});
        return;
    }
    value = *wanted;
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "%1 is now %2"), {args[0], QString::number(value)});
}

void QcCheatCommands::gamemode(const QcCommandContext &context)
{
    const QStringList &args = context.args;
    QcServerClient *self = context.source.player();
    if (args.isEmpty()) {
        if (!self) {
            context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /gamemode [survival|creative] [player]"));
            return;
        }
        context.reply(modeOfPlayer(self->gameMode), {self->name});
        return;
    }

    if (!context.source.may(QcPermission::Operator)) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Only operators can use /%1"), {u"gamemode"_s});
        return;
    }

    const std::optional<QcGameMode> mode = QcGameModes::parse(args[0]);
    if (!mode) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Unknown game mode %1; use survival or creative"), {args[0]});
        return;
    }

    QList<QcServerClient *> targets;
    if (args.size() >= 2) {
        targets = QcCommandArguments::players(m_state, args[1]);
        if (targets.isEmpty()) {
            context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "No player named %1 is online"), {args[1]});
            return;
        }
    } else if (self) {
        targets.append(self);
    } else {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "The console must name a player: /gamemode <mode> <player>"));
        return;
    }

    for (QcServerClient *target : std::as_const(targets)) {
        setGamemode(context, *target, *mode);
    }
}

void QcCheatCommands::setGamemode(const QcCommandContext &context, QcServerClient &target, QcGameMode mode)
{
    m_parts.gameModes.setMode(target, mode);
    context.reply(modeSet(mode), {target.name});
    if (&target != context.source.player()) {
        target.connection->send(QcChatText::message(modeNow(mode)));
    }
}

void QcCheatCommands::defaultGamemode(const QcCommandContext &context)
{
    if (context.args.isEmpty()) {
        context.reply(newPlayersStart(m_parts.gameModes.defaultMode()));
        return;
    }

    const std::optional<QcGameMode> mode = QcGameModes::parse(context.args[0]);
    if (!mode) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Unknown game mode %1; use survival or creative"), {context.args[0]});
        return;
    }

    // Saved with the world; a server.json setting no longer overrides it until the server restarts.
    m_state.info().defaultGameMode = *mode;
    m_state.info().unknownGameMode.clear();
    m_state.config().defaultGameMode.reset();
    context.reply(newPlayersStart(*mode));
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
        if (std::max(std::abs(dx), std::abs(dz)) == radius && m_state.world().generator().biomeAt(feet.x + dx, feet.z + dz) == name) {
            return dz;
        }
    }
    return std::nullopt;
}
