#include "server/commands/QcStandCommands.h"

#include "common/gameplay/QcArmour.h"
#include "common/physics/QcPhysics.h"
#include "common/physics/QcRaycast.h"
#include "common/protocol/QcChatText.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcServerInventory.h"
#include "server/gameplay/QcServerStands.h"
#include "server/gameplay/QcStandProperties.h"

#include <QtMath>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

QString qualified(const QString &name)
{
    return name.contains(u':') ? name : u"qtcraft:"_s + name;
}

void reply(const QcServerClient &client, const char *key, const QStringList &arguments = {})
{
    client.connection->send(QcChatText::message(key, arguments));
}

const char *const usage = QT_TRANSLATE_NOOP("QcServerMessages",
                                            "Usage: /armorstand <nearest|looking|id> <pose <part> <x> <y> <z>|rotation <degrees>|"
                                            "arms <true|false>|plate <true|false>|small <true|false>|showname <true|false>|name [text]|"
                                            "equip <item|set>|cape <cape|none>|clear|reset|info>");

} // namespace

QcStandCommands::QcStandCommands(QcServerState &state, QcServerStands &stands, QcServerInventory &inventory)
    : m_state(state)
    , m_stands(stands)
    , m_inventory(inventory)
{
}

QcArmourStand *QcStandCommands::standInView(const QcServerClient &client)
{
    const double yaw = qDegreesToRadians(double(client.yaw));
    const double pitch = qDegreesToRadians(double(client.pitch));
    const QcVec3 direction{std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
    const QcVec3 eye = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};

    QcArmourStand *best = nullptr;
    double nearest = targetRange;
    for (const QcArmourStand &stand : m_state.entities().stands().stands()) {
        const std::optional<double> distance = QcRaycast::intersectBox(eye, direction, QcArmourStandManager::bodyOf(stand), nearest);
        if (distance && *distance < nearest) {
            nearest = *distance;
            best = m_state.entities().stands().find(stand.id);
        }
    }
    return best;
}

QcArmourStand *QcStandCommands::target(const QcServerClient &client, const QString &word)
{
    if (word == u"looking"_s) {
        return standInView(client);
    }

    bool isId = false;
    const quint32 id = word.toUInt(&isId);
    QcArmourStand *best = nullptr;
    double nearest = targetRange;
    for (const QcArmourStand &stand : m_state.entities().stands().stands()) {
        const double distance = (stand.motion.position - client.motion.position).length();
        const bool wanted = isId ? stand.id == id : word == u"nearest"_s;
        if (wanted && distance <= nearest) {
            nearest = distance;
            best = m_state.entities().stands().find(stand.id);
        }
    }
    return best;
}

void QcStandCommands::armorStand(QcServerClient &client, const QStringList &words)
{
    if (words.size() < 3) {
        reply(client, usage);
        return;
    }

    QcArmourStand *stand = target(client, words[1]);
    if (!stand) {
        reply(client, QT_TRANSLATE_NOOP("QcServerMessages", "No armour stand found: %1"), {words[1]});
        return;
    }

    if (words[2] == u"info"_s) {
        reply(client, QT_TRANSLATE_NOOP("QcServerMessages", "Armour stand %1: %2"), {QString::number(stand->id), describe(*stand)});
        return;
    }
    const QcArmourStand before = *stand;
    if (!QcStandProperties::apply(m_state.content(), *stand, words.mid(2))) {
        reply(client, usage);
        return;
    }

    // Armour and held items that `equip` or `clear` replaced fall where the stand stands.
    m_stands.dropReplaced(before, *stand);
    m_stands.changed(*stand);
    reply(client, QT_TRANSLATE_NOOP("QcServerMessages", "Armour stand %1 changed"), {QString::number(stand->id)});
}

std::optional<QcVec3> QcStandCommands::summonPosition(const QcServerClient &client) const
{
    const double yaw = qDegreesToRadians(double(client.yaw));
    const QcVec3 ahead = client.motion.position + QcVec3{std::sin(yaw) * 3.0, 0.0, -std::cos(yaw) * 3.0};
    const QcBlockPos column = QcPhysics::blockContaining(ahead);

    // The first block with a solid top within a few metres below (or the player's height) carries it.
    for (int dy = 2; dy >= -8; --dy) {
        const QcBlockPos below = column.offset(0, dy - 1, 0);
        const std::optional<QcBlockId> block = m_state.world().blockAt(below);
        if (block && m_state.content().isValidBlock(*block) && m_state.content().block(*block).solid) {
            return QcVec3{column.x + 0.5, double(column.y + dy), column.z + 0.5};
        }
    }
    return std::nullopt;
}

bool QcStandCommands::summon(QcServerClient &client, const QStringList &words)
{
    const std::optional<QcItemId> item = m_state.content().itemId(qualified(words.value(1)));
    if (!item || !m_state.content().item(*item).placesStand) {
        return false;
    }

    QcArmourStand stand;
    stand.item = m_state.content().item(*item).name;
    stand.yaw = QcServerStands::facingYaw(client.yaw);
    const QStringList options = words.mid(2);
    for (const QString &option : options) {
        // Values of a property follow '=' separated by commas; "small" alone means small=true.
        const QString property = option.section(u'=', 0, 0);
        const QcContentRegistry &content = m_state.content();
        const bool applied = option.contains(u'=')
                                 ? QcStandProperties::apply(content, stand, QStringList{property} + option.section(u'=', 1).split(u','))
                                 : QcStandProperties::apply(content, stand, {property}) || QcStandProperties::apply(content, stand, {property, u"true"_s});
        if (!applied) {
            reply(client, QT_TRANSLATE_NOOP("QcServerMessages", "Invalid armour stand option: %1"), {option});
            return true;
        }
    }

    const std::optional<QcVec3> position = summonPosition(client);
    if (!position) {
        reply(client, QT_TRANSLATE_NOOP("QcServerMessages", "No ground for an armour stand in front of you"));
        return true;
    }

    stand.motion.position = *position;
    const QcArmourStand *spawned = m_stands.spawn(stand);
    if (!spawned) {
        reply(client, QT_TRANSLATE_NOOP("QcServerMessages", "Too many armour stands in the world (at most %1)"),
              {QString::number(m_state.entities().stands().limit())});
        return true;
    }
    reply(client, QT_TRANSLATE_NOOP("QcServerMessages", "Summoned armour stand %1"), {QString::number(spawned->id)});
    return true;
}

void QcStandCommands::equip(QcServerClient &client, const QStringList &words)
{
    const QString &name = words.value(1);
    QcEquipment equipment = client.equipment;
    if (name == u"clear"_s) {
        equipment = {};
    } else {
        const QList<QcItemStack> items = QcStandProperties::itemsNamed(m_state.content(), name);
        bool any = false;
        for (const QcItemStack &stack : items) {
            if (const QcArmourDefinition *armour = QcArmour::of(m_state.content(), stack.item)) {
                equipment.at(armour->slot) = stack;
                any = true;
            }
        }
        if (!any) {
            reply(client, QT_TRANSLATE_NOOP("QcServerMessages", "Unknown armour or armour set: %1"), {name});
            return;
        }
    }

    // Pieces taken off (replaced or cleared) go into the inventory, or are thrown without room.
    for (std::size_t slot = 0; slot < QcEquipment::slotCount; ++slot) {
        const QcItemStack &worn = client.equipment.stacks[slot];
        if (worn != equipment.stacks[slot]) {
            m_inventory.giveOrThrow(client, worn);
        }
    }
    m_inventory.setEquipment(client, equipment);
    m_inventory.send(client);
    reply(client, QT_TRANSLATE_NOOP("QcServerMessages", "Armour: %1 points"), {QString::number(QcArmour::points(m_state.content(), equipment))});
}

QString QcStandCommands::describe(const QcArmourStand &stand)
{
    QStringList parts;
    for (std::size_t i = 0; i < QcStandPose::partCount; ++i) {
        const QcStandPose::Rotation &rotation = stand.pose.parts[i];
        parts.append(u"%1 %2 %3 %4"_s.arg(QcStandPose::partName(QcStandPose::Part(i))).arg(rotation.x).arg(rotation.y).arg(rotation.z));
    }
    const auto flag = [](bool on) { return on ? u"true"_s : u"false"_s; };
    const QString cape = stand.cape.isEmpty() ? u"none"_s : stand.cape;
    return u"rotation %1, arms %2, plate %3, small %4, showname %5, name '%6', cape %7; %8"_s.arg(stand.yaw)
        .arg(flag(stand.pose.arms), flag(stand.pose.basePlate), flag(stand.pose.small), flag(stand.pose.showName), stand.name, cape,
             parts.join(u", "_s));
}
