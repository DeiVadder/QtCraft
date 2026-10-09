#include "QcControlEntityCommands.h"

#include "QcClientSession.h"
#include "QcContentRegistry.h"
#include "QcControlAim.h"
#include "QcControlArguments.h"
#include "QcControlCommandTable.h"
#include "QcControlContext.h"
#include "QcPhysics.h"

#include <QJsonArray>
#include <QtMath>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

qint64 entityArgument(const QJsonObject &request)
{
    return request.value(u"entity"_s).toInteger(-1);
}

QJsonObject entityJson(const QcContentRegistry *content, const QcClientEntity &entity, double distance)
{
    if (entity.isItem()) {
        const bool known = content && content->isValidItem(entity.item.item);
        return {{u"id"_s, qint64(entity.id)},
                {u"type"_s, u"item"_s},
                {u"item"_s, known ? content->item(entity.item.item).name : QString()},
                {u"count"_s, entity.item.count},
                {u"position"_s, QcControlReports::vectorJson(entity.position)},
                {u"distance"_s, distance}};
    }

    if (entity.isFallingBlock()) {
        return {{u"id"_s, qint64(entity.id)},
                {u"type"_s, u"fallingBlock"_s},
                {u"block"_s, content ? content->stateName(entity.block) : QString()},
                {u"position"_s, QcControlReports::vectorJson(entity.position)},
                {u"distance"_s, distance}};
    }

    const bool mob = content && entity.mobType >= 0 && entity.mobType < content->mobs().size();
    return {{u"id"_s, qint64(entity.id)},
            {u"type"_s, mob ? content->mobs()[entity.mobType].name : u"player"_s},
            {u"name"_s, entity.name},
            {u"hostile"_s, mob && content->mobs()[entity.mobType].hostile},
            {u"position"_s, QcControlReports::vectorJson(entity.position)},
            {u"distance"_s, distance},
            {u"health"_s, entity.health}};
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
    const auto it = session().entities().constFind(quint32(wanted));
    if (wanted < 0 || it == session().entities().constEnd()) {
        m_context.fail(request, u"No entity with this id nearby"_s);
        return false;
    }

    const auto content = session().content();
    const double height = it->mobType >= 0 && it->mobType < content->mobs().size() ? content->mobs()[it->mobType].height
                                                                                   : QcPhysics::playerHeight;

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
    const double healthBefore = session().entities().constFind(quint32(wanted))->health;

    m_context.begin({request, QcControlActiveCommand::timeoutPolls(6), [this, startTick, sent, wanted, healthBefore]() mutable {
                         if (!sent) {
                             if (session().tickCount() <= startTick) {
                                 return false;
                             }
                             if (session().targetEntity() != std::optional<quint32>(quint32(wanted)) || !session().attack()) {
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
                         const auto target = session().entities().constFind(quint32(wanted));
                         if (target != session().entities().constEnd() && target->health >= healthBefore) {
                             m_context.setActiveError(u"The hit did not land (attack cooldown or out of reach)"_s);
                         }
                         return true;
                     }});
}

void QcControlEntityCommands::useEntity(const QJsonObject &request)
{
    // Turn towards the creature, then use the held item on it (catch a firefly in a bottle).
    const quint64 startTick = session().tickCount();
    if (!turnToEntity(request)) {
        return;
    }

    const qint64 wanted = entityArgument(request);
    bool sent = false;

    m_context.begin({request, QcControlActiveCommand::timeoutPolls(6), [this, startTick, sent, wanted]() mutable {
                         if (!sent) {
                             if (session().tickCount() <= startTick) {
                                 return false;
                             }
                             if (session().targetEntity() != std::optional<quint32>(quint32(wanted)) || !session().useItem()) {
                                 m_context.setActiveError(u"Entity is not in reach or not in sight"_s);
                                 return true;
                             }
                             sent = true;
                             return false;
                         }

                         if (session().tickCount() < startTick + 4) {
                             return false;
                         }

                         if (session().entities().contains(quint32(wanted))) {
                             m_context.setActiveError(u"Nothing happened (the held item does not catch this creature)"_s);
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
                         return walkToNearestItem(radius, session().tickCount() >= startTick + quint64(maxTicks));
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
        const QcVec3 delta = nearest->position - me;
        intent.yaw = float(qRadiansToDegrees(std::atan2(delta.x, -delta.z)));
        const double horizontal = std::hypot(delta.x, delta.z);
        // Walk until the item is within the pickup reach, jump onto steps.
        intent.forward = horizontal > 0.4 ? 1.0f : 0.0f;
        intent.jump = horizontal > 0.4 && session().playerMotion().onGround && delta.y > 0.6;
    } else {
        intent.forward = 0.0f;
        intent.jump = false;
    }

    session().setIntent(intent);
    return done;
}
