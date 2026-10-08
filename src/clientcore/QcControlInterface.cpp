#include "QcControlInterface.h"

#include <utility>

#include "QcClientSession.h"
#include "QcContentRegistry.h"
#include "QcCrafting.h"
#include "QcDayCycle.h"
#include "QcPhysics.h"

#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaEnum>
#include <QTimer>
#include <QtMath>

#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr int pollIntervalMs = 50;
constexpr int maxBlockQueryRadius = 8;
constexpr int maxCommandTicks = 20 * 600;
constexpr int defaultReadyTimeoutPolls = 20 * 60;

// Tick-based commands fail when the session stops ticking (e.g. while loading) for much longer than asked.
int timeoutPolls(int ticks)
{
    return ticks * 4 + 100;
}

QJsonArray vectorJson(const QcVec3 &v)
{
    return {v.x, v.y, v.z};
}

int intField(const QJsonObject &request, const QString &key, int fallback)
{
    return request.value(key).toInt(fallback);
}

} // namespace

QcControlInterface::QcControlInterface(QcClientSession *session, QIODevice *input, QIODevice *output, QObject *parent)
    : QObject(parent)
    , m_session(session)
    , m_input(input)
    , m_output(output)
{
    if (m_input) {
        connect(m_input, &QIODevice::readyRead, this, &QcControlInterface::readInput);
    }
    setSession(session);
    auto *poll = new QTimer(this);
    poll->setInterval(pollIntervalMs);
    connect(poll, &QTimer::timeout, this, &QcControlInterface::onTick);
    poll->start();
}

void QcControlInterface::setSession(QcClientSession *session)
{
    if (m_session) {
        disconnect(m_session, nullptr, this, nullptr);
    }
    m_session = session;
    if (m_session) {
        connect(m_session, &QcClientSession::chatReceived, this, [this](const QString &text) { m_chatLog.append(text); });
    }
}

void QcControlInterface::readInput()
{
    feed(m_input->readAll());
}

void QcControlInterface::feed(const QByteArray &data)
{
    m_lineBuffer.append(data);
    qsizetype newline = m_lineBuffer.indexOf('\n');
    while (newline >= 0) {
        const QByteArray line = m_lineBuffer.left(newline).trimmed();
        m_lineBuffer.remove(0, newline + 1);
        newline = m_lineBuffer.indexOf('\n');
        ++m_lineNumber;
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }
        QJsonParseError error{};
        const QJsonDocument document = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            // Queued like a request so the error is answered in input order.
            const QString reason = error.error != QJsonParseError::NoError ? error.errorString() : u"not an object"_s;
            submit({{QLatin1StringView(parseErrorKey), u"Invalid JSON request in line %1: %2"_s.arg(m_lineNumber).arg(reason)}});
            continue;
        }
        submit(document.object());
    }
}

void QcControlInterface::submit(const QJsonObject &request)
{
    m_queue.enqueue(request);
    processQueue();
}

void QcControlInterface::processQueue()
{
    while (!m_active && !m_queue.isEmpty()) {
        start(m_queue.dequeue());
    }
    if (isIdle()) {
        emit idle();
    }
}

void QcControlInterface::finish(const QJsonObject &request, QJsonObject response)
{
    if (request.contains(u"id"_s)) {
        response.insert(u"id"_s, request.value(u"id"_s));
    }
    response.insert(u"ok"_s, true);
    if (m_output) {
        m_output->write(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n');
    }
}

void QcControlInterface::fail(const QJsonObject &request, const QString &message)
{
    ++m_failedRequests;
    QJsonObject response{{u"ok"_s, false}, {u"error"_s, message}};
    if (request.contains(u"id"_s)) {
        response.insert(u"id"_s, request.value(u"id"_s));
    }
    if (request.contains(u"cmd"_s)) {
        response.insert(u"cmd"_s, request.value(u"cmd"_s));
    }
    if (m_output) {
        m_output->write(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n');
    }
}

QJsonObject QcControlInterface::playerStatus() const
{
    QJsonObject status;
    const QMetaEnum stateEnum = QMetaEnum::fromType<QcClientSession::State>();
    status.insert(u"state"_s, QString::fromLatin1(stateEnum.valueToKey(int(m_session->state()))));
    status.insert(u"tick"_s, qint64(m_session->tickCount()));
    status.insert(u"world"_s, m_session->worldName());
    status.insert(u"seed"_s, QString::number(m_session->seed()));
    const QcEntityMotion &motion = m_session->playerMotion();
    status.insert(u"position"_s, vectorJson(motion.position));
    status.insert(u"velocity"_s, vectorJson(motion.velocity));
    status.insert(u"onGround"_s, motion.onGround);
    status.insert(u"inWater"_s, motion.inWater);
    status.insert(u"yaw"_s, m_session->intent().yaw);
    status.insert(u"pitch"_s, m_session->intent().pitch);
    status.insert(u"loadedChunks"_s, m_session->world() ? qint64(m_session->world()->chunkCount()) : 0);
    if (m_session->state() == QcClientSession::State::Disconnected) {
        status.insert(u"disconnectReason"_s, m_session->disconnectReason());
    }
    if (const auto &target = m_session->target()) {
        const std::optional<QcBlockId> block = m_session->world() ? m_session->world()->blockAt(target->block) : std::nullopt;
        status.insert(u"target"_s, QJsonObject{{u"x"_s, target->block.x},
                                               {u"y"_s, target->block.y},
                                               {u"z"_s, target->block.z},
                                               {u"face"_s, faceName(target->face)},
                                               {u"block"_s, block ? m_session->content()->block(*block).name : QString()}});
    }
    status.insert(u"health"_s, m_session->health());
    status.insert(u"maxHealth"_s, m_session->maxHealth());
    status.insert(u"dead"_s, m_session->isDead());
    status.insert(u"worldTime"_s, m_session->worldTime());
    status.insert(u"night"_s, QcDayCycle::isNight(m_session->worldTime()));
    if (const std::optional<quint32> entity = m_session->targetEntity()) {
        status.insert(u"targetEntity"_s, qint64(*entity));
    }
    const QcItemStack held = m_session->inventory().selectedStack();
    status.insert(u"selectedSlot"_s, m_session->inventory().selectedHotbarSlot());
    status.insert(u"held"_s, stackJson(held));
    if (!m_chatLog.isEmpty()) {
        status.insert(u"messages"_s, QJsonArray::fromStringList(m_chatLog));
    }
    return status;
}

QJsonValue QcControlInterface::stackJson(QcItemStack stack) const
{
    const auto content = m_session->content();
    if (stack.isEmpty() || !content || !content->isValidItem(stack.item)) {
        return QJsonValue();
    }
    QJsonObject json{{u"item"_s, content->item(stack.item).name}, {u"count"_s, stack.count}};
    if (stack.damage > 0) {
        json.insert(u"damage"_s, stack.damage);
    }
    return json;
}

QJsonObject QcControlInterface::inventoryJson() const
{
    QJsonArray stacks;
    for (int i = 0; i < QcInventory::slotCount; ++i) {
        const QJsonValue stack = stackJson(m_session->inventory().slot(i));
        if (!stack.isNull()) {
            QJsonObject entry = stack.toObject();
            entry.insert(u"slot"_s, i);
            stacks.append(entry);
        }
    }
    QJsonArray grid;
    for (qsizetype i = 0; i < m_session->craftingGrid().size(); ++i) {
        const QJsonValue stack = stackJson(m_session->craftingGrid()[i]);
        if (!stack.isNull()) {
            QJsonObject entry = stack.toObject();
            entry.insert(u"slot"_s, int(36 + i));
            grid.append(entry);
        }
    }
    return {{u"selectedSlot"_s, m_session->inventory().selectedHotbarSlot()},
            {u"slots"_s, stacks},
            {u"craftingSize"_s, m_session->craftingSize()},
            {u"craftingGrid"_s, grid},
            {u"craftingResult"_s, stackJson(m_session->craftingResult())}};
}

QcBlockPos QcControlInterface::blockArgument(const QJsonObject &request)
{
    return {intField(request, u"x"_s, 0), intField(request, u"y"_s, 0), intField(request, u"z"_s, 0)};
}

std::optional<QcBlockFace> QcControlInterface::faceArgument(const QJsonObject &request)
{
    const QString name = request.value(u"face"_s).toString(u"up"_s);
    for (const QcBlockFace face : qcAllBlockFaces) {
        if (faceName(face) == name) {
            return face;
        }
    }
    return std::nullopt;
}

QString QcControlInterface::faceName(QcBlockFace face)
{
    static const std::array<QString, qcBlockFaceCount> names = {u"east"_s, u"west"_s, u"up"_s, u"down"_s, u"south"_s, u"north"_s};
    return names[std::size_t(face)];
}

void QcControlInterface::lookAt(QcBlockPos block, std::optional<QcBlockFace> face)
{
    // Aim at the block centre, or just inside the centre of the given face.
    QcVec3 point{block.x + 0.5, block.y + 0.5, block.z + 0.5};
    if (face) {
        const QcFaceOffset normal = qcFaceOffset(*face);
        point += QcVec3{normal.dx * 0.49, normal.dy * 0.49, normal.dz * 0.49};
    }
    lookAtPoint(point);
}

void QcControlInterface::lookAtPoint(const QcVec3 &point)
{
    const QcVec3 eye = m_session->playerMotion().position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const QcVec3 delta = point - eye;
    QcPlayerInput intent = m_session->intent();
    intent.yaw = float(qRadiansToDegrees(std::atan2(delta.x, -delta.z)));
    intent.pitch = float(qRadiansToDegrees(std::atan2(delta.y, std::sqrt(delta.x * delta.x + delta.z * delta.z))));
    m_session->setIntent(intent);
}

void QcControlInterface::start(const QJsonObject &request)
{
    if (!m_session) {
        fail(request, u"No session"_s);
        return;
    }
    if (request.contains(QLatin1StringView(parseErrorKey))) {
        fail({}, request.value(QLatin1StringView(parseErrorKey)).toString());
        return;
    }
    m_chatLog.clear();
    for (const char *key :
         {"yaw", "pitch", "forward", "strafe", "ticks", "radius", "x", "y", "z", "timeoutPolls", "slot", "from", "to", "maxTicks", "entity", "count"}) {
        const QJsonValue value = request.value(QLatin1StringView(key));
        if (!value.isUndefined() && !value.isDouble()) {
            fail(request, u"'%1' must be a number"_s.arg(QLatin1StringView(key)));
            return;
        }
    }
    for (const char *key : {"jump", "sneak", "sprint", "all"}) {
        const QJsonValue value = request.value(QLatin1StringView(key));
        if (!value.isUndefined() && !value.isBool()) {
            fail(request, u"'%1' must be true or false"_s.arg(QLatin1StringView(key)));
            return;
        }
    }
    const QString command = request.value(u"cmd"_s).toString();
    const quint64 startTick = m_session->tickCount();
    const auto ticksArgument = [&](int fallback) { return std::clamp(intField(request, u"ticks"_s, fallback), 0, maxCommandTicks); };

    if (command == u"status"_s) {
        finish(request, playerStatus());
    } else if (command == u"look"_s) {
        QcPlayerInput intent = m_session->intent();
        // Wrapped in double precision first: huge values would overflow a float.
        intent.yaw = float(std::fmod(request.value(u"yaw"_s).toDouble(intent.yaw), 360.0));
        intent.pitch = float(std::clamp(request.value(u"pitch"_s).toDouble(intent.pitch), -90.0, 90.0));
        m_session->setIntent(intent);
        finish(request, playerStatus());
    } else if (command == u"move"_s || command == u"jump"_s) {
        const bool isJump = command == u"jump"_s;
        QcPlayerInput intent = m_session->intent();
        intent.forward = float(std::clamp(request.value(u"forward"_s).toDouble(0.0), -1.0, 1.0));
        intent.strafe = float(std::clamp(request.value(u"strafe"_s).toDouble(0.0), -1.0, 1.0));
        intent.jump = isJump || request.value(u"jump"_s).toBool(false);
        intent.sneak = request.value(u"sneak"_s).toBool(false);
        intent.sprint = request.value(u"sprint"_s).toBool(false);
        m_session->setIntent(intent);
        const int ticks = ticksArgument(isJump ? 2 : 20);
        m_active = ActiveCommand{request, timeoutPolls(ticks), [this, startTick, ticks] {
                                     if (m_session->tickCount() < startTick + quint64(ticks)) {
                                         return false;
                                     }
                                     QcPlayerInput stop = m_session->intent();
                                     stop.forward = 0.0f;
                                     stop.strafe = 0.0f;
                                     stop.jump = false;
                                     stop.sneak = false;
                                     stop.sprint = false;
                                     m_session->setIntent(stop);
                                     return true;
                                 }};
    } else if (command == u"lookAt"_s) {
        lookAt(blockArgument(request), std::nullopt);
        finish(request, playerStatus());
    } else if (command == u"mine"_s) {
        std::optional<QcBlockPos> pos;
        if (request.contains(u"x"_s)) {
            pos = blockArgument(request);
            lookAt(*pos, std::nullopt);
        } else if (m_session->target()) {
            pos = m_session->target()->block;
        }
        if (!pos || !m_session->world()) {
            fail(request, u"No block targeted"_s);
            return;
        }
        m_session->setMining(true);
        const int maxTicks = std::clamp(intField(request, u"maxTicks"_s, 400), 1, maxCommandTicks);
        m_active = ActiveCommand{request, timeoutPolls(maxTicks), [this, pos] {
                                     const std::optional<QcBlockId> block = m_session->world()->blockAt(*pos);
                                     if (block && *block != qcAirBlockId) {
                                         return false;
                                     }
                                     m_session->setMining(false);
                                     return true;
                                 }};
    } else if (command == u"place"_s || command == u"use"_s) {
        if (request.contains(u"x"_s)) {
            const std::optional<QcBlockFace> face = faceArgument(request);
            if (!face) {
                fail(request, u"'face' must be east, west, up, down, south or north"_s);
                return;
            }
            lookAt(blockArgument(request), face);
        }
        // Wait one tick so the session's target follows the new view direction, then use the item.
        bool used = false;
        m_active = ActiveCommand{request, timeoutPolls(4), [this, startTick, used]() mutable {
                                     if (!used) {
                                         if (m_session->tickCount() <= startTick) {
                                             return false;
                                         }
                                         if (!m_session->useItem()) {
                                             return true;
                                         }
                                         used = true;
                                         return false;
                                     }
                                     return m_session->tickCount() >= startTick + 4;
                                 }};
    } else if (command == u"entities"_s) {
        const int radius = std::clamp(intField(request, u"radius"_s, 32), 1, 64);
        const QcVec3 me = m_session->playerMotion().position;
        const auto content = m_session->content();
        QJsonArray list;
        for (const QcClientEntity &entity : m_session->entities()) {
            const double distance = (entity.position - me).length();
            if (distance > radius) {
                continue;
            }
            if (entity.isItem()) {
                const bool known = content && content->isValidItem(entity.item.item);
                list.append(QJsonObject{{u"id"_s, qint64(entity.id)},
                                        {u"type"_s, u"item"_s},
                                        {u"item"_s, known ? content->item(entity.item.item).name : QString()},
                                        {u"count"_s, entity.item.count},
                                        {u"position"_s, vectorJson(entity.position)},
                                        {u"distance"_s, distance}});
                continue;
            }
            const bool mob = content && entity.mobType >= 0 && entity.mobType < content->mobs().size();
            list.append(QJsonObject{{u"id"_s, qint64(entity.id)},
                                    {u"type"_s, mob ? content->mobs()[entity.mobType].name : u"player"_s},
                                    {u"name"_s, entity.name},
                                    {u"hostile"_s, mob && content->mobs()[entity.mobType].hostile},
                                    {u"position"_s, vectorJson(entity.position)},
                                    {u"distance"_s, distance},
                                    {u"health"_s, entity.health}});
        }
        finish(request, {{u"entities"_s, list}});
    } else if (command == u"attack"_s) {
        // Turn towards the entity's centre, then hit it on the next tick.
        const qint64 wanted = request.value(u"entity"_s).toInteger(-1);
        const auto it = m_session->entities().constFind(quint32(wanted));
        if (wanted < 0 || it == m_session->entities().constEnd()) {
            fail(request, u"No entity with this id nearby"_s);
            return;
        }
        const auto content = m_session->content();
        const double height = it->mobType >= 0 && it->mobType < content->mobs().size() ? content->mobs()[it->mobType].height
                                                                                       : QcPhysics::playerHeight;
        lookAtPoint(it->position + QcVec3{0.0, height * 0.5, 0.0});
        bool sent = false;
        const double healthBefore = it->health;
        m_active = ActiveCommand{request, timeoutPolls(6), [this, startTick, sent, wanted, healthBefore]() mutable {
                                     if (!sent) {
                                         if (m_session->tickCount() <= startTick) {
                                             return false;
                                         }
                                         if (m_session->targetEntity() != std::optional<quint32>(quint32(wanted))
                                             || !m_session->attack()) {
                                             m_activeError = u"Entity is not in reach or not in sight"_s;
                                             return true;
                                         }
                                         sent = true;
                                         return false;
                                     }
                                     if (m_session->tickCount() < startTick + 4) {
                                         return false;
                                     }
                                     // The server drops hits during the attack cooldown or out of its reach.
                                     const auto target = m_session->entities().constFind(quint32(wanted));
                                     if (target != m_session->entities().constEnd() && target->health >= healthBefore) {
                                         m_activeError = u"The hit did not land (attack cooldown or out of reach)"_s;
                                     }
                                     return true;
                                 }};
    } else if (command == u"view"_s) {
        // Without a mode it only reports the current one.
        if (request.contains(u"mode"_s)) {
            const std::optional<QcCameraMode> mode = QcViewCamera::fromName(request.value(u"mode"_s).toString());
            if (!mode) {
                fail(request, u"'mode' must be first, behind or front"_s);
                return;
            }
            m_session->setCameraMode(*mode);
        }
        finish(request, {{u"mode"_s, QString::fromLatin1(QcViewCamera::name(m_session->cameraMode()))}});
    } else if (command == u"drop"_s) {
        if (!m_session->dropItem(request.value(u"all"_s).toBool(false))) {
            fail(request, u"Nothing selected to drop"_s);
            return;
        }
        m_active = ActiveCommand{request, timeoutPolls(3), [this, startTick] { return m_session->tickCount() >= startTick + 3; }};
    } else if (command == u"collect"_s) {
        // Walks to the nearest dropped item within the radius until none is left or time runs out.
        const double radius = std::clamp(intField(request, u"radius"_s, 8), 1, 32);
        const int maxTicks = std::clamp(intField(request, u"maxTicks"_s, 200), 1, maxCommandTicks);
        m_active = ActiveCommand{request, timeoutPolls(maxTicks), [this, radius, startTick, maxTicks] {
                                     const QcVec3 me = m_session->playerMotion().position;
                                     const QcClientEntity *nearest = nullptr;
                                     for (const QcClientEntity &entity : m_session->entities()) {
                                         const double distance = (entity.position - me).length();
                                         if (entity.isItem() && distance <= radius
                                             && (!nearest || distance < (nearest->position - me).length())) {
                                             nearest = &entity;
                                         }
                                     }
                                     QcPlayerInput intent = m_session->intent();
                                     const bool done = !nearest || m_session->tickCount() >= startTick + quint64(maxTicks);
                                     if (!done) {
                                         const QcVec3 delta = nearest->position - me;
                                         intent.yaw = float(qRadiansToDegrees(std::atan2(delta.x, -delta.z)));
                                         const double horizontal = std::hypot(delta.x, delta.z);
                                         // Walk until the item is within the pickup reach, jump onto steps.
                                         intent.forward = horizontal > 0.4 ? 1.0f : 0.0f;
                                         intent.jump = horizontal > 0.4 && m_session->playerMotion().onGround && delta.y > 0.6;
                                     } else {
                                         intent.forward = 0.0f;
                                         intent.jump = false;
                                     }
                                     m_session->setIntent(intent);
                                     return done;
                                 }};
    } else if (command == u"respawn"_s) {
        m_session->respawn();
        m_active = ActiveCommand{request, timeoutPolls(10), [this] { return !m_session->isDead(); }};
    } else if (command == u"select"_s) {
        m_session->selectHotbarSlot(intField(request, u"slot"_s, 0));
        finish(request, inventoryJson());
    } else if (command == u"inventory"_s) {
        finish(request, inventoryJson());
    } else if (command == u"moveItem"_s) {
        m_session->moveInventoryItem(intField(request, u"from"_s, 0), intField(request, u"to"_s, 0),
                                     std::max(0, intField(request, u"count"_s, 0)));
        m_active = ActiveCommand{request, timeoutPolls(3), [this, startTick] { return m_session->tickCount() >= startTick + 3; }};
    } else if (command == u"closeCrafting"_s) {
        // Returns the grid to the inventory and leaves a crafting table's 3 x 3 grid.
        m_session->closeCrafting();
        m_active = ActiveCommand{request, timeoutPolls(3), [this, startTick] { return m_session->tickCount() >= startTick + 3; }};
    } else if (command == u"mods"_s) {
        // Content packs the server loaded, base game first.
        QJsonArray packs;
        if (const auto content = m_session->content()) {
            for (const QcPackReference &pack : content->packs()) {
                packs.append(QJsonObject{{u"id"_s, pack.id}, {u"name"_s, pack.name}, {u"version"_s, pack.version}});
            }
        }
        finish(request, {{u"packs"_s, packs}});
    } else if (command == u"recipes"_s) {
        // Every recipe with what it needs and how often the inventory could make it right now.
        const auto content = m_session->content();
        QJsonArray list;
        for (const QcRecipeDefinition &recipe : content ? content->recipes() : QList<QcRecipeDefinition>()) {
            int possible = 64;
            QJsonObject needs;
            const QList<std::pair<QString, int>> needed = QcCrafting::requirements(recipe);
            for (const auto &[name, count] : std::as_const(needed)) {
                needs.insert(name, count);
                const std::optional<QcItemId> item = content->itemId(name);
                possible = std::min(possible, item ? m_session->inventory().countOf(*item) / count : 0);
            }
            if (request.value(u"all"_s).toBool(false) || possible > 0) {
                list.append(QJsonObject{{u"recipe"_s, recipe.name},
                                        {u"result"_s, recipe.resultItem},
                                        {u"count"_s, recipe.resultCount},
                                        {u"needs"_s, needs},
                                        {u"needsCraftingTable"_s, QcCrafting::gridSizeNeeded(recipe) > 2},
                                        {u"possible"_s, possible}});
            }
        }
        finish(request, {{u"recipes"_s, list}});
    } else if (command == u"craft"_s) {
        m_session->craftRecipe(request.value(u"recipe"_s).toString(), std::clamp(intField(request, u"count"_s, 1), 1, 64));
        m_active = ActiveCommand{request, timeoutPolls(3), [this, startTick] { return m_session->tickCount() >= startTick + 3; }};
    } else if (command == u"takeResult"_s) {
        m_session->takeCraftingResult();
        m_active = ActiveCommand{request, timeoutPolls(3), [this, startTick] { return m_session->tickCount() >= startTick + 3; }};
    } else if (command == u"chat"_s) {
        m_chatLog.clear();
        m_session->sendChat(request.value(u"text"_s).toString());
        m_active = ActiveCommand{request, timeoutPolls(3), [this, startTick] { return m_session->tickCount() >= startTick + 3; }};
    } else if (command == u"wait"_s) {
        const int ticks = ticksArgument(20);
        m_active = ActiveCommand{request, timeoutPolls(ticks), [this, startTick, ticks] {
                                     return m_session->tickCount() >= startTick + quint64(ticks);
                                 }};
    } else if (command == u"waitUntilReady"_s) {
        const int timeoutPolls = std::clamp(intField(request, u"timeoutPolls"_s, defaultReadyTimeoutPolls), 1, maxCommandTicks);
        m_active = ActiveCommand{request, timeoutPolls, [this] {
                                     const QcClientWorld *world = m_session->world();
                                     if (m_session->state() != QcClientSession::State::Playing || !world) {
                                         return false;
                                     }
                                     const QcVec3 p = m_session->playerMotion().position;
                                     const QcChunkPos centre =
                                         QcBlockPos{int(std::floor(p.x)), 0, int(std::floor(p.z))}.chunk();
                                     for (int dx = -1; dx <= 1; ++dx) {
                                         for (int dz = -1; dz <= 1; ++dz) {
                                             if (!world->chunk({centre.x + dx, centre.z + dz})) {
                                                 return false;
                                             }
                                         }
                                     }
                                     // Settled: the player stands on something or floats in water.
                                     return m_session->playerMotion().onGround || m_session->playerMotion().inWater;
                                 }};
    } else if (command == u"block"_s || command == u"blocks"_s) {
        const QcClientWorld *world = m_session->world();
        if (!world) {
            fail(request, u"World not loaded yet"_s);
            return;
        }
        const QcContentRegistry &content = world->content();
        const auto describe = [&](QcBlockPos pos) -> QJsonValue {
            const std::optional<QcBlockId> id = world->blockAt(pos);
            return id ? QJsonValue(content.block(*id).name) : QJsonValue();
        };
        if (command == u"block"_s) {
            const QcBlockPos pos{intField(request, u"x"_s, 0), intField(request, u"y"_s, 0), intField(request, u"z"_s, 0)};
            finish(request, {{u"block"_s, describe(pos)}});
            return;
        }
        const int radius = std::clamp(intField(request, u"radius"_s, 3), 0, maxBlockQueryRadius);
        const QcVec3 p = m_session->playerMotion().position;
        const QcBlockPos centre{int(std::floor(p.x)), int(std::floor(p.y)), int(std::floor(p.z))};
        QJsonArray blocks;
        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dz = -radius; dz <= radius; ++dz) {
                    const QcBlockPos pos = centre.offset(dx, dy, dz);
                    const std::optional<QcBlockId> id = world->blockAt(pos);
                    if (id && *id != qcAirBlockId) {
                        blocks.append(QJsonObject{{u"x"_s, pos.x}, {u"y"_s, pos.y}, {u"z"_s, pos.z},
                                                  {u"block"_s, content.block(*id).name}});
                    }
                }
            }
        }
        finish(request, {{u"centre"_s, QJsonArray{centre.x, centre.y, centre.z}}, {u"blocks"_s, blocks}});
    } else if (command == u"quit"_s) {
        finish(request, {});
        emit quitRequested();
    } else {
        QJsonObject response;
        if (m_extension && m_extension(request, response)) {
            if (response.value(u"ok"_s).toBool(true)) {
                finish(request, response);
            } else {
                fail(request, response.value(u"error"_s).toString());
            }
            return;
        }
        fail(request, u"Unknown command '%1'"_s.arg(command));
    }
}

void QcControlInterface::onTick()
{
    if (!m_active) {
        return;
    }
    if (!m_session) {
        const QJsonObject request = m_active->request;
        m_active.reset();
        fail(request, u"Session ended"_s);
        processQueue();
        return;
    }
    if (m_session->state() == QcClientSession::State::Disconnected) {
        const QJsonObject request = m_active->request;
        m_active.reset();
        fail(request, u"Disconnected: %1"_s.arg(m_session->disconnectReason()));
        processQueue();
        return;
    }
    if (m_active->step()) {
        const QJsonObject request = m_active->request;
        const QString error = std::exchange(m_activeError, {});
        m_active.reset();
        if (error.isEmpty()) {
            finish(request, playerStatus());
        } else {
            fail(request, error);
        }
        processQueue();
        return;
    }
    if (--m_active->remainingTicks <= 0) {
        // A timed-out mine command must not keep mining in the background.
        m_session->setMining(false);
        const QJsonObject request = m_active->request;
        m_active.reset();
        fail(request, u"Timed out"_s);
        processQueue();
    }
}
