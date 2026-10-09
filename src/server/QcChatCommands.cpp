#include "QcChatCommands.h"

#include "QcBlockRules.h"
#include "QcBlockUpdateQueue.h"
#include "QcDayCycle.h"
#include "QcPhysics.h"
#include "QcServerBlocks.h"
#include "QcServerCombat.h"
#include "QcServerInventory.h"
#include "QcServerState.h"

#include <QScopeGuard>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

// Names without a namespace mean the base game's.
QString qualified(const QString &name)
{
    return name.contains(u':') ? name : u"qtcraft:"_s + name;
}

} // namespace

struct QcChatCommands::Command
{
    QcServerClient &client;
    const QStringList &words;
    const QString &text;

    void reply(const QString &line) const { client.connection->send(QcMsg::ChatMessage{line}); }
};

struct QcChatCommands::BlockArea
{
    QcBlockPos from;
    QcBlockPos low;
    QcBlockPos high;
    QString name;
    QcBlockId block = qcAirBlockId;
    // Without explicit states, connections follow the neighbours.
    bool explicitState = false;
};

QcChatCommands::QcChatCommands(QcServerState &state, const Parts &parts, Actions actions)
    : m_state(state)
    , m_parts(parts)
    , m_actions(std::move(actions))
{
}

const QcChatCommands::Entry *QcChatCommands::find(const QString &name)
{
    static const std::array<Entry, 13> commands{{
        {u"help", 1, false, [](QcChatCommands &chat, const Command &command) { chat.help(command); }},
        {u"gamerule", 2, false, [](QcChatCommands &chat, const Command &command) { chat.gamerule(command); }},
        {u"time", 3, true, [](QcChatCommands &chat, const Command &command) { chat.time(command); }},
        {u"summon", 2, true, [](QcChatCommands &chat, const Command &command) { chat.summon(command); }},
        {u"spawnitem", 2, true, [](QcChatCommands &chat, const Command &command) { chat.spawnItem(command); }},
        {u"setblock", 5, true, [](QcChatCommands &chat, const Command &command) { chat.setBlock(command); }},
        {u"fill", 8, true, [](QcChatCommands &chat, const Command &command) { chat.fill(command); }},
        {u"locatebiome", 2, false, [](QcChatCommands &chat, const Command &command) { chat.locateBiome(command); }},
        {u"kill", 1, true, [](QcChatCommands &chat, const Command &command) { chat.kill(command); }},
        {u"feed", 1, true, [](QcChatCommands &chat, const Command &command) { chat.feed(command); }},
        {u"heal", 1, true, [](QcChatCommands &chat, const Command &command) { chat.heal(command); }},
        {u"save", 1, false, [](QcChatCommands &chat, const Command &command) { chat.save(command); }},
        {u"give", 2, true, [](QcChatCommands &chat, const Command &command) { chat.give(command); }},
    }};

    const auto found = std::ranges::find(commands, QStringView(name), &Entry::name);
    return found != commands.end() ? &*found : nullptr;
}

void QcChatCommands::handle(QcServerClient &client, const QString &text)
{
    const QStringList words = text.split(u' ', Qt::SkipEmptyParts);
    if (words.isEmpty()) {
        return;
    }

    if (!words.first().startsWith(u'/')) {
        broadcast(client, text);
        return;
    }

    const Command command{client, words, text};
    const Entry *entry = find(words.first().mid(1).toLower());
    if (entry && entry->cheat && !m_state.config.allowCheats) {
        command.reply(u"Cheats are disabled on this server"_s);
    } else if (!entry || words.size() < entry->minWords) {
        command.reply(u"Unknown command: %1"_s.arg(words.first()));
    } else {
        entry->run(*this, command);
    }
}

void QcChatCommands::broadcast(const QcServerClient &client, const QString &text)
{
    for (const auto &other : m_state.clients) {
        if (other->state == QcServerClient::State::Playing) {
            other->connection->send(QcMsg::ChatMessage{u"<%1> %2"_s.arg(client.name, text)});
        }
    }
}

void QcChatCommands::help(const Command &command)
{
    command.reply(u"Commands: /help, /give <item> [count], /save, /time set <day|night|seconds>, /time add <seconds>, "
                  u"/summon <mob> [count], /kill, /heal, /gamerule keepInventory [true|false], /spawnitem <item> [count], "
                  u"/setblock <x> <y> <z> <block>[states], /fill <x1> <y1> <z1> <x2> <y2> <z2> <block> (~ for relative coordinates), "
                  u"/feed [food], /locatebiome <biome>"_s);
}

void QcChatCommands::gamerule(const Command &command)
{
    const QStringList &words = command.words;
    if (words[1] != u"keepInventory"_s) {
        command.reply(u"Unknown command: %1"_s.arg(words.first()));
    } else if (words.size() == 2) {
        command.reply(u"keepInventory is %1"_s.arg(m_state.info.keepInventory ? u"true"_s : u"false"_s));
    } else if (!m_state.config.allowCheats) {
        command.reply(u"Cheats are disabled on this server"_s);
    } else if (words[2] == u"true"_s || words[2] == u"false"_s) {
        m_state.info.keepInventory = words[2] == u"true"_s;
        command.reply(u"keepInventory is now %1"_s.arg(words[2]));
    } else {
        command.reply(u"Usage: /gamerule keepInventory [true|false]"_s);
    }
}

void QcChatCommands::time(const Command &command) const
{
    const QStringList &words = command.words;
    bool ok = true;
    double seconds = 0.0;
    const double dayStart = std::floor(m_state.info.worldTime / QcDayCycle::dayLength) * QcDayCycle::dayLength;

    if (words[1] == u"set"_s && words[2] == u"day"_s) {
        seconds = dayStart + QcDayCycle::dayLength * 1.05;
    } else if (words[1] == u"set"_s && words[2] == u"night"_s) {
        seconds = dayStart + QcDayCycle::dayLength * 0.6;
    } else if (words[1] == u"set"_s) {
        seconds = words[2].toDouble(&ok);
    } else if (words[1] == u"add"_s) {
        seconds = m_state.info.worldTime + words[2].toDouble(&ok);
    } else {
        ok = false;
    }

    if (!ok || !std::isfinite(seconds) || seconds < 0.0 || seconds > 1e9) {
        command.reply(u"Usage: /time set <day|night|seconds> or /time add <seconds>"_s);
        return;
    }

    m_actions.setWorldTime(seconds);
    command.reply(u"Time is now %1 s (%2)"_s.arg(qRound(seconds)).arg(QcDayCycle::isNight(seconds) ? u"night"_s : u"day"_s));
}

void QcChatCommands::summon(const Command &command)
{
    const QStringList &words = command.words;
    const QString name = qualified(words[1]);
    const std::optional<int> type = m_state.content->mobIndex(name);
    bool countOk = true;
    const int count = words.size() >= 3 ? words[2].toInt(&countOk) : 1;
    if (!type || !countOk || count < 1 || count > 16) {
        command.reply(u"Unknown mob or invalid count: %1"_s.arg(command.text));
        return;
    }

    if (m_state.mobs.awakeCount() + count > QcMobManager::summonLimit) {
        command.reply(u"Too many creatures in the world (at most %1)"_s.arg(QcMobManager::summonLimit));
        return;
    }

    // Three metres in front of the player.
    const double yaw = qDegreesToRadians(double(command.client.yaw));
    const QcVec3 position = command.client.motion.position + QcVec3{std::sin(yaw) * 3.0, 0.5, -std::cos(yaw) * 3.0};
    for (int i = 0; i < count; ++i) {
        const int row = i / 3; // NOLINT(bugprone-integer-division): grid row
        // Summoned animals stay like natural ones.
        const QcMobDefinition &summoned = m_state.content->mobs()[*type];
        m_state.mobs.spawn(*type, position + QcVec3{(i % 3) * 0.8, 0.0, row * 0.8}, m_state.newEntityId(), false,
                           !summoned.hostile && !summoned.ambient && !summoned.flying);
    }

    command.reply(u"Summoned %1 x %2"_s.arg(count).arg(name));
}

void QcChatCommands::spawnItem(const Command &command)
{
    const QStringList &words = command.words;
    const QString name = qualified(words[1]);
    const std::optional<QcItemId> item = m_state.content->itemId(name);
    bool countOk = true;
    const int count = words.size() >= 3 ? words[2].toInt(&countOk) : 1;
    if (!item || !countOk || count < 1 || count > 64) {
        command.reply(u"Unknown item or invalid count: %1"_s.arg(command.text));
        return;
    }

    // Two metres in front of the player, on the ground.
    const double yaw = qDegreesToRadians(double(command.client.yaw));
    const QcVec3 position = command.client.motion.position + QcVec3{std::sin(yaw) * 2.0, 0.5, -std::cos(yaw) * 2.0};
    m_state.items.spawnScattered({*item, count, 0}, position, m_state.newEntityId());
    command.reply(u"Dropped %1 x %2"_s.arg(count).arg(name));
}

void QcChatCommands::setBlock(const Command &command)
{
    placeBlocks(command, false);
}

void QcChatCommands::fill(const Command &command)
{
    placeBlocks(command, true);
}

void QcChatCommands::placeBlocks(const Command &command, bool fill)
{
    const std::optional<BlockArea> area = parseBlockArea(command, fill);
    if (!area) {
        return;
    }

    const bool machine = m_state.machines.machineOf(area->block) != nullptr;

    // The changes and their cascades queue behind the updates of player actions.
    m_parts.updates.setCommandMode(true);
    const auto normalMode = qScopeGuard([this] { m_parts.updates.setCommandMode(false); });

    int changed = 0;
    for (int y = area->low.y; y <= area->high.y; ++y) {
        for (int z = area->low.z; z <= area->high.z; ++z) {
            for (int x = area->low.x; x <= area->high.x; ++x) {
                changed += changeBlock({x, y, z}, *area, machine) ? 1 : 0;
            }
        }
    }

    if (changed == 0) {
        command.reply(u"Nothing set: the chunks are not loaded%1"_s.arg(machine ? u" or there are too many machines"_s : QString()));
        return;
    }

    const QcBlockPos &from = area->from;
    command.reply(fill ? u"Filled %1 blocks with %2"_s.arg(changed).arg(area->name)
                       : u"Set %1 at %2 %3 %4"_s.arg(area->name).arg(from.x).arg(from.y).arg(from.z));
}

std::optional<QcChatCommands::BlockArea> QcChatCommands::parseBlockArea(const Command &command, bool fill) const
{
    const QStringList &words = command.words;
    const QcBlockPos feet = QcPhysics::blockContaining(command.client.motion.position);
    const auto coordinate = [](const QString &word, int origin, bool &ok) {
        if (word.startsWith(u'~')) {
            ok = true;
            return origin + (word.size() > 1 ? QStringView(word).mid(1).toInt(&ok) : 0);
        }
        return word.toInt(&ok);
    };

    bool ok = true;
    const auto position = [&](int first) {
        bool okX = false;
        bool okY = false;
        bool okZ = false;
        const QcBlockPos pos{coordinate(words[first], feet.x, okX), coordinate(words[first + 1], feet.y, okY),
                             coordinate(words[first + 2], feet.z, okZ)};
        ok = ok && okX && okY && okZ;
        return pos;
    };

    BlockArea area;
    area.from = position(1);
    const QcBlockPos to = fill ? position(4) : area.from;
    const QString &text = words[fill ? 7 : 4];
    // "name" or "name[property=value,...]"; without states, connections follow the neighbours.
    area.name = text.section(u'[', 0, 0).contains(u':') ? text : u"qtcraft:"_s + text;
    area.explicitState = area.name.contains(u'[');

    QStringList problems;
    const std::optional<QcBlockId> block = m_state.content->stateId(area.name, &problems);
    if (block && !problems.isEmpty()) {
        command.reply(problems.join(u"; "_s));
        return std::nullopt;
    }

    area.low = {std::min(area.from.x, to.x), std::min(area.from.y, to.y), std::min(area.from.z, to.z)};
    area.high = {std::max(area.from.x, to.x), std::max(area.from.y, to.y), std::max(area.from.z, to.z)};
    const qint64 volume = qint64(area.high.x - area.low.x + 1) * (area.high.y - area.low.y + 1) * (area.high.z - area.low.z + 1);
    if (!ok || !block || !area.low.isInsideWorldHeight() || !area.high.isInsideWorldHeight() || volume > maxFillBlocks) {
        command.reply(u"Usage: /setblock <x> <y> <z> <block>, /fill <x1> <y1> <z1> <x2> <y2> <z2> <block> (at most %1 blocks; "
                      u"block may carry states: name[property=value,...]) in loaded chunks"_s.arg(maxFillBlocks));
        return std::nullopt;
    }

    area.block = *block;
    return area;
}

bool QcChatCommands::changeBlock(QcBlockPos pos, const BlockArea &area, bool machine)
{
    if (!m_state.world.blockAt(pos) || (machine && !m_state.machines.find(pos) && m_state.machines.isFull())) {
        return false;
    }

    if (area.block == qcAirBlockId) {
        m_parts.blocks.breakBlock(pos, false);
        return true;
    }

    m_parts.blocks.setAndBroadcast(pos, area.explicitState ? area.block : QcBlockRules::connectedState(m_state.world, pos, area.block));

    if (m_state.content->block(area.block).sapling.isSapling()) {
        m_state.plants.saplingPlaced(pos, area.block);
    }

    if (machine) {
        m_state.machines.add(pos);
    }
    return true;
}

void QcChatCommands::locateBiome(const Command &command)
{
    // Nearest column of the biome on growing squares around the player, in steps of 8 blocks.
    const QString name = qualified(command.words[1]);
    const QcBlockPos feet = QcPhysics::blockContaining(command.client.motion.position);

    for (int radius = 0; radius <= biomeSearchRadius; radius += 8) {
        for (int dx = -radius; dx <= radius; dx += 8) {
            if (const std::optional<int> dz = findBiomeAlong(feet, radius, dx, name)) {
                command.reply(u"%1 at %2 %3"_s.arg(name).arg(feet.x + dx).arg(feet.z + *dz));
                return;
            }
        }
    }

    command.reply(u"No %1 within %2 blocks"_s.arg(name).arg(biomeSearchRadius));
}

std::optional<int> QcChatCommands::findBiomeAlong(QcBlockPos feet, int radius, int dx, const QString &name) const
{
    for (int dz = -radius; dz <= radius; dz += 8) {
        if (std::max(std::abs(dx), std::abs(dz)) == radius && m_state.world.generator().biomeAt(feet.x + dx, feet.z + dz) == name) {
            return dz;
        }
    }
    return std::nullopt;
}

void QcChatCommands::kill(const Command &command)
{
    command.client.hurtTimer = 0.0;
    m_parts.combat.damagePlayer(command.client, command.client.health, {});
}

void QcChatCommands::feed(const Command &command)
{
    if (command.client.dead) {
        command.reply(u"Respawn first"_s);
        return;
    }

    bool ok = true;
    const int food = command.words.size() >= 2 ? command.words[1].toInt(&ok) : QcHunger::maxFood;
    if (!ok || food < 0 || food > QcHunger::maxFood) {
        command.reply(u"Usage: /feed [0..%1]"_s.arg(QcHunger::maxFood));
        return;
    }

    command.client.hunger.restore(food, 0.0);
    m_parts.combat.sendFood(command.client);
    command.reply(u"Food is now %1"_s.arg(food));
}

void QcChatCommands::heal(const Command &command)
{
    if (command.client.dead) {
        command.reply(u"Respawn first"_s);
        return;
    }

    command.client.health = QcServerClient::maxHealth;
    m_parts.combat.sendHealth(command.client);
    command.reply(u"Healed"_s);
}

void QcChatCommands::save(const Command &command) const
{
    const QcStatus saved = m_actions.save();
    command.reply(saved ? u"World saved"_s : u"Saving failed: %1"_s.arg(saved.error()));
}

void QcChatCommands::give(const Command &command)
{
    const QString name = qualified(command.words[1]);
    const std::optional<QcItemId> item = m_state.content->itemId(name);
    bool countOk = true;
    const int count = command.words.size() >= 3 ? command.words[2].toInt(&countOk) : 1;
    if (!item || !countOk || count < 1 || count > 64 * QcInventory::slotCount) {
        command.reply(u"Unknown item or invalid count: %1"_s.arg(command.text));
        return;
    }

    const int leftover = command.client.inventory.add(*item, count, *m_state.content);
    m_parts.inventory.send(command.client);
    command.reply(u"Gave %1 x %2"_s.arg(count - leftover).arg(name));
}
