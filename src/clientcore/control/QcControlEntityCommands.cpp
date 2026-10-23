#include "clientcore/control/QcControlEntityCommands.h"

#include "clientcore/control/QcControlAim.h"
#include "clientcore/control/QcControlArguments.h"
#include "clientcore/control/QcControlCommandTable.h"
#include "clientcore/control/QcControlContext.h"
#include "clientcore/control/QcControlReports.h"
#include "clientcore/session/QcClientSession.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/physics/QcPhysics.h"
#include "common/world/QcMinecartBody.h"

#include <QJsonArray>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// Ticks after using an item on a creature by which a catch or a feeding shows on the client.
constexpr int useSettleTicks = 12;

qint64 entityArgument(const QJsonObject &request)
{
    return request.value(u"entity"_s).toInteger(-1);
}

// The worn pieces by slot; empty slots are null.
QJsonObject armourJson(const QcContentRegistry *content, const QcEquipment &equipment)
{
    static constexpr std::array slotNames{"head", "chest", "legs", "feet"};
    QJsonObject armour;
    for (std::size_t slot = 0; slot < QcEquipment::slotCount; ++slot) {
        const QcItemStack &stack = equipment.stacks[slot];
        const bool known = !stack.isEmpty() && content && content->isValidItem(stack.item);
        armour.insert(QLatin1StringView(slotNames[slot]), known ? QJsonValue(content->item(stack.item).name) : QJsonValue());
    }
    return armour;
}

// What the character model animates (QcEntityPose at the start of the current tick).
QJsonObject animationJson(const QcClientEntity &entity)
{
    const QcEntityPose pose = QcLocalAnimation::poseOf(entity, 0.0);
    return {{u"speed"_s, pose.speed},
            {u"walkDistance"_s, pose.walkDistance},
            {u"onGround"_s, pose.onGround},
            {u"inWater"_s, pose.inWater},
            {u"sneaking"_s, pose.sneaking},
            {u"sprinting"_s, pose.sprinting},
            {u"swinging"_s, pose.swingProgress >= 0.0},
            {u"hurt"_s, pose.hurtProgress >= 0.0}};
}

// A projectile (decision E-425): the item it looks like and whether it sticks.
QJsonObject projectileJson(const QcContentRegistry *content, const QcClientEntity &entity, double distance)
{
    const bool known = content && content->isValidItem(entity.item.item);
    return {{u"id"_s, static_cast<qint64>(entity.id)},
            {u"type"_s, u"projectile"_s},
            {u"item"_s, known ? content->item(entity.item.item).name : QString()},
            {u"stuck"_s, (entity.flags & QcMsg::Stuck) != 0},
            {u"position"_s, QcControlReports::vectorJson(entity.position)},
            {u"distance"_s, distance}};
}

// A falling or moving block or a lit explosive (decision E-604): drawn as its block.
QJsonObject blockEntityJson(const QcContentRegistry *content, const QcClientEntity &entity, double distance)
{
    QString type = u"fallingBlock"_s;
    if (entity.isMovingBlock()) {
        type = u"movingBlock"_s;
    } else if (entity.isPrimedExplosive()) {
        type = u"primedExplosive"_s;
    }
    return {{u"id"_s, static_cast<qint64>(entity.id)},
            {u"type"_s, type},
            {u"block"_s, content ? content->stateName(entity.block) : QString()},
            {u"position"_s, QcControlReports::vectorJson(entity.position)},
            {u"distance"_s, distance}};
}

QJsonObject entityJson(const QcContentRegistry *content, const QcClientEntity &entity, double distance)
{
    if (entity.isItem()) {
        const bool known = content && content->isValidItem(entity.item.item);
        return {{u"id"_s, static_cast<qint64>(entity.id)},
                {u"type"_s, u"item"_s},
                {u"item"_s, known ? content->item(entity.item.item).name : QString()},
                {u"count"_s, entity.item.count},
                {u"position"_s, QcControlReports::vectorJson(entity.position)},
                {u"distance"_s, distance}};
    }

    if (entity.isMinecart()) {
        const bool known = content && content->isValidItem(entity.item.item);
        return {{u"id"_s, static_cast<qint64>(entity.id)},
                {u"type"_s, u"minecart"_s},
                {u"item"_s, known ? content->item(entity.item.item).name : QString()},
                {u"yaw"_s, entity.yaw},
                {u"position"_s, QcControlReports::vectorJson(entity.position)},
                {u"distance"_s, distance}};
    }

    if (entity.isExperienceOrb()) {
        return {{u"id"_s, static_cast<qint64>(entity.id)},
                {u"type"_s, u"experienceOrb"_s},
                {u"value"_s, static_cast<int>(entity.health)},
                {u"position"_s, QcControlReports::vectorJson(entity.position)},
                {u"distance"_s, distance}};
    }

    if (entity.isProjectile()) {
        return projectileJson(content, entity, distance);
    }

    if (entity.isBlockEntity()) {
        return blockEntityJson(content, entity, distance);
    }

    const QcMobDefinition *creature = content ? entity.creature(*content) : nullptr;
    QJsonObject json{{u"id"_s, static_cast<qint64>(entity.id)},
                     {u"type"_s, creature ? creature->name : u"player"_s},
                     {u"name"_s, entity.name},
                     {u"hostile"_s, creature && creature->hostile},
                     {u"position"_s, QcControlReports::vectorJson(entity.position)},
                     {u"distance"_s, distance},
                     {u"health"_s, entity.health},
                     {u"yaw"_s, entity.yaw},
                     {u"animation"_s, animationJson(entity)},
                     {u"burning"_s, (entity.flags & QcMsg::Burning) != 0}};
    if (creature) {
        json.insert(u"young"_s, (entity.flags & QcMsg::Young) != 0);
        json.insert(u"inLove"_s, (entity.flags & QcMsg::InLove) != 0);
    }
    if (entity.isPlayer()) {
        json.insert(u"appearance"_s, QcControlReports::appearanceJson(entity.appearance));
        json.insert(u"armour"_s, armourJson(content, entity.equipment));
        json.insert(u"held"_s, entity.held != qcNoItem && content && content->isValidItem(entity.held) ? QJsonValue(content->item(entity.held).name)
                                                                                                         : QJsonValue());
    }
    return json;
}

} // namespace

QcControlEntityCommands::QcControlEntityCommands(QcControlContext &context)
    : m_context(context)
{
}

void QcControlEntityCommands::registerCommands(QcControlCommandTable &table)
{
    table.add(u"entities"_s, [this](const QJsonObject &request) { entities(request); });
    table.add(u"attack"_s, [this](const QJsonObject &request) { attack(request); });
    table.add(u"useEntity"_s, [this](const QJsonObject &request) { useEntity(request); });
    table.add(u"collect"_s, [this](const QJsonObject &request) { collect(request); });
    table.add(u"ride"_s, [this](const QJsonObject &request) { ride(request); });
    table.add(u"leave"_s, [this](const QJsonObject &request) { leave(request); });
}

QcClientSession &QcControlEntityCommands::session() const
{
    return *m_context.session();
}

void QcControlEntityCommands::entities(const QJsonObject &request)
{
    const int radius = std::clamp(QcControlArguments::intField(request, u"radius"_s, 32), 1, 64);
    const QcVec3 me = session().playerMotion().position;
    const auto content = session().content();

    QJsonArray list;
    for (const QcClientEntity &entity : session().entities()) {
        const double distance = (entity.position - me).length();
        if (distance <= radius) {
            list.append(entityJson(content.get(), entity, distance));
        }
    }

    m_context.finish(request, {{u"entities"_s, list}});
}

bool QcControlEntityCommands::turnToEntity(const QJsonObject &request)
{
    const qint64 wanted = entityArgument(request);
    const auto it = session().entities().constFind(static_cast<quint32>(wanted));
    if (wanted < 0 || it == session().entities().constEnd()) {
        m_context.fail(request, u"No entity with this id nearby"_s);
        return false;
    }

    const auto content = session().content();
    const QcMobDefinition *creature = it->creature(*content);
    const double height = it->isMinecart() ? QcMinecartBody::height : creature ? creature->height : QcPhysics::playerHeight;

    QcControlAim::lookAtPoint(session(), it->position + QcVec3{0.0, height * 0.5, 0.0});
    return true;
}

void QcControlEntityCommands::attack(const QJsonObject &request)
{
    // Turn towards the entity's centre, then hit it on the next tick.
    const quint64 startTick = session().tickCount();
    if (!turnToEntity(request)) {
        return;
    }

    const qint64 wanted = entityArgument(request);
    bool sent = false;
    const double healthBefore = session().entities().constFind(static_cast<quint32>(wanted))->health;

    m_context.begin({request, QcControlActiveCommand::timeoutPolls(6), [this, startTick, sent, wanted, healthBefore]() mutable {
                         if (!sent) {
                             if (session().tickCount() <= startTick) {
                                 return false;
                             }
                             if (session().hand().targetEntity() != std::optional<quint32>(static_cast<quint32>(wanted)) || !session().attack()) {
                                 m_context.setActiveError(u"Entity is not in reach or not in sight"_s);
                                 return true;
                             }
                             sent = true;
                             return false;
                         }

                         if (session().tickCount() < startTick + 4) {
                             return false;
                         }

                         // The server drops hits during the attack cooldown or out of its reach.
                         const auto target = session().entities().constFind(static_cast<quint32>(wanted));
                         if (target != session().entities().constEnd() && target->health >= healthBefore) {
                             m_context.setActiveError(u"The hit did not land (attack cooldown or out of reach)"_s);
                         }
                         return true;
                     }});
}

void QcControlEntityCommands::useEntity(const QJsonObject &request)
{
    // Turn towards the creature, then use the held item on it (catch a firefly in a bottle, feed an
    // animal).
    const quint64 startTick = session().tickCount();
    if (!turnToEntity(request)) {
        return;
    }

    const qint64 wanted = entityArgument(request);
    const UseBefore before{session().inventory().selectedStack(), session().entities().value(static_cast<quint32>(wanted)).flags};
    std::optional<quint64> sentTick;

    m_context.begin({request, QcControlActiveCommand::timeoutPolls(useSettleTicks), [this, startTick, sentTick, wanted, before]() mutable {
                         if (!sentTick) {
                             if (session().tickCount() <= startTick) {
                                 return false;
                             }
                             if (session().hand().targetEntity() != std::optional<quint32>(static_cast<quint32>(wanted)) || !session().useItem()) {
                                 m_context.setActiveError(u"Entity is not in reach or not in sight"_s);
                                 return true;
                             }
                             sentTick = session().tickCount();
                             return false;
                         }

                         // Counted from the use, not from the command: under load the client sends it
                         // ticks late. The held item comes back at once, a caught creature leaves with
                         // the next entity update, so a used item beside a creature still there waits
                         // a few ticks more before it counts as refused.
                         const quint64 ticks = session().tickCount() - *sentTick;
                         const bool stillThere = session().entities().contains(static_cast<quint32>(wanted));
                         const QcItemStack &held = session().inventory().selectedStack();
                         const bool heldChanged = held.item != before.held.item || held.count != before.held.count;
                         if (ticks < 4 || (heldChanged && stillThere && ticks < useSettleTicks)) {
                             return false;
                         }
                         if (const QString error = useEntityError(static_cast<quint32>(wanted), before); !error.isEmpty()) {
                             m_context.setActiveError(error);
                         }
                         return true;
                     }});
}

QString QcControlEntityCommands::useEntityError(quint32 entityId, const UseBefore &before) const
{
    // Caught creatures vanish; fed adults fall in love; fed young ones take the item (also from a
    // creative hand, which keeps it).
    const auto entity = session().entities().constFind(entityId);
    if (entity == session().entities().constEnd()) {
        return {};
    }

    const QcContentRegistry *content = session().content().get();
    const QcItemStack &held = session().inventory().selectedStack();
    const QcMobDefinition *creature = content ? entity->creature(*content) : nullptr;
    const bool food = creature && content->isValidItem(before.held.item) && creature->breeding.food.contains(content->item(before.held.item).name);
    const bool used = held.item != before.held.item || held.count != before.held.count;
    const bool fellInLove = (entity->flags & QcMsg::InLove) != 0 && (before.flags & QcMsg::InLove) == 0;
    const bool youngFed = food && (before.flags & QcMsg::Young) != 0 && (used || session().gameMode().rules().infiniteItems);
    if (fellInLove || youngFed) {
        return {};
    }
    return used ? u"The creature did not take it (the item was eaten or put on)"_s
                : u"Nothing happened (the held item does not catch or feed this creature now)"_s;
}

void QcControlEntityCommands::ride(const QJsonObject &request)
{
    // Turn towards the cart and use it; done when the server names it as the player's vehicle.
    const quint64 startTick = session().tickCount();
    if (!turnToEntity(request)) {
        return;
    }

    const quint32 wanted = static_cast<quint32>(entityArgument(request));
    bool sent = false;
    m_context.begin({request, QcControlActiveCommand::timeoutPolls(20), [this, startTick, sent, wanted]() mutable {
                         if (!sent) {
                             if (session().tickCount() <= startTick) {
                                 return false;
                             }
                             if (session().hand().targetEntity() != std::optional<quint32>(wanted) || !session().useItem()) {
                                 m_context.setActiveError(u"Minecart is not in reach or not in sight"_s);
                                 return true;
                             }
                             sent = true;
                             return false;
                         }

                         if (session().prediction().riding().vehicle() == wanted) {
                             return true;
                         }
                         if (session().tickCount() >= startTick + 20) {
                             m_context.setActiveError(u"The server did not let the player in (another rider, or no minecart)"_s);
                             return true;
                         }
                         return false;
                     }});
}

void QcControlEntityCommands::leave(const QJsonObject &request)
{
    // Sneak until the server lets the player out of the cart.
    if (!session().prediction().riding().isRiding()) {
        m_context.fail(request, u"The player rides no minecart"_s);
        return;
    }

    const quint64 startTick = session().tickCount();
    QcPlayerInput intent = session().intent();
    intent.sneak = true;
    session().setIntent(intent);
    m_context.begin({request, QcControlActiveCommand::timeoutPolls(20), [this, startTick] {
                         const bool out = !session().prediction().riding().isRiding();
                         if (!out && session().tickCount() < startTick + 20) {
                             return false;
                         }
                         QcPlayerInput released = session().intent();
                         released.sneak = false;
                         session().setIntent(released);
                         if (!out) {
                             m_context.setActiveError(u"The player is still in the minecart"_s);
                         }
                         return true;
                     }});
}

void QcControlEntityCommands::collect(const QJsonObject &request)
{
    // Walks to the nearest dropped item within the radius until none is left or time runs out.
    const quint64 startTick = session().tickCount();
    const double radius = std::clamp(QcControlArguments::intField(request, u"radius"_s, 8), 1, 32);
    const int maxTicks = std::clamp(QcControlArguments::intField(request, u"maxTicks"_s, 200), 1, QcControlArguments::maxCommandTicks);

    m_context.begin({request, QcControlActiveCommand::timeoutPolls(maxTicks), [this, radius, startTick, maxTicks] {
                         return walkToNearestItem(radius, session().tickCount() >= startTick + static_cast<quint64>(maxTicks));
                     }});
}

bool QcControlEntityCommands::walkToNearestItem(double radius, bool timeUp)
{
    const QcVec3 me = session().playerMotion().position;
    const QcClientEntity *nearest = nullptr;
    for (const QcClientEntity &entity : session().entities()) {
        const double distance = (entity.position - me).length();
        if (entity.isItem() && distance <= radius && (!nearest || distance < (nearest->position - me).length())) {
            nearest = &entity;
        }
    }

    QcPlayerInput intent = session().intent();
    const bool done = !nearest || timeUp;
    if (!done) {
        // An item lower down (in a hole) is reached by stepping into the middle of its block, where
        // the player fits; others by walking up to the item itself.
        QcVec3 target = nearest->position;
        const bool below = target.y < me.y - 0.5;
        if (below) {
            target = {std::floor(target.x) + 0.5, target.y, std::floor(target.z) + 0.5};
        }
        const QcVec3 delta = target - me;
        intent.yaw = static_cast<float>(qRadiansToDegrees(std::atan2(delta.x, -delta.z)));
        const double horizontal = std::hypot(delta.x, delta.z);
        // Walk until the item is within the pickup reach, jump onto steps.
        const double close = below ? 0.1 : 0.4;
        intent.forward = horizontal > close ? (below ? 0.4f : 1.0f) : 0.0f;
        intent.jump = horizontal > close && session().playerMotion().onGround && delta.y > 0.6;
    } else {
        intent.forward = 0.0f;
        intent.jump = false;
    }

    session().setIntent(intent);
    return done;
}
