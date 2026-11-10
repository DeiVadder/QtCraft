// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/animals/QcServerBarter.h"

#include "common/content/registry/QcContentLoot.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/physics/QcPhysics.h"
#include "server/advancements/QcServerAdvancements.h"
#include "server/animals/QcServerAnimals.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/gameplay/QcServerCombat.h"
#include "server/gameplay/QcServerInventory.h"
#include "server/mobbrains/QcMobSteering.h"
#include "server/pathing/QcMobNavigator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace {

// How fast a bartered stack flies from the creature, in m/s, and how much it rises.
constexpr double throwSpeed = 2.4;
constexpr double throwLift = 2.0;

// The barter settings of `mob`'s kind in `content`; nullptr for a kind that does not barter.
const QcBarterDefinition *barterOf(const QcContentRegistry &content, const QcCreature &mob)
{
    if (!mob.barter || !content.mobs().contains(mob.type)) {
        return nullptr;
    }
    const std::optional<QcBarterDefinition> &barter = content.mobs()[mob.type].barter;
    return barter ? &*barter : nullptr;
}

// Whether `barter` loves `item` (a name in `content`).
bool loves(const QcContentRegistry &content, const QcBarterDefinition &barter, QcItemId item)
{
    return content.isValidItem(item) && barter.loves.contains(content.item(item).name);
}

// Whether `mob` keeps another stack of `item` in its hoard: the currency always goes, other items
// while there is room.
bool hasRoomFor(const QcContentRegistry &content, const QcCreature &mob, const QcBarterDefinition &barter, QcItemId item)
{
    return content.item(item).name == barter.currency || mob.barter->hoard.size() < barter.hoardSize;
}

QcVec3 eyesOf(const QcServerClient &client)
{
    return client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
}

} // namespace

QcServerBarter::QcServerBarter(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory)
    : m_state(state)
    , m_sounds(sounds)
    , m_inventory(inventory)
    , m_departures(state, [this](const QcCreature &mob, QcMobManager::Departure why) {
        if (why == QcMobManager::Departure::Died) {
            dropHeld(mob);
        }
    })
{
}

bool QcServerBarter::mayTake(const QcCreature &mob)
{
    return !mob.dormant && !mob.isYoung() && !mob.noAi && mob.provokedBy == 0 && !mob.barter->admires();
}

QcServerBarter::Giving QcServerBarter::give(QcServerClient &client, quint32 mobId)
{
    QcCreature *mob = m_state.entities().creatures().find(mobId);
    const QcItemDefinition *held = m_state.heldItem(client);
    const QcBarterDefinition *barter = mob ? barterOf(m_state.content(), *mob) : nullptr;
    if (!barter || !held || held->name != barter->currency) {
        return Giving::NotTheCurrency;
    }
    const QcMobDefinition &definition = m_state.content().mobs()[mob->type];
    const QcVec3 centre = mob->motion.position + QcVec3{0.0, definition.height * 0.5, 0.0};
    if (!mayTake(*mob) || (centre - eyesOf(client)).length() > QcServerCombat::attackReach + definition.width + 1.0
        || !QcLineOfSight::canSee(m_state.world(), client, QcServerAnimals::shownBox(definition, *mob))) {
        return Giving::Refused;
    }
    const QcItemStack stack = client.inventory.selectedStack().withCount(1);
    if (!client.rules().infiniteItems) {
        client.inventory.removeFromSlot(client.inventory.selectedHotbarSlot(), 1);
    }
    m_inventory.send(client);
    take(*mob, *barter, stack, client.id);
    return Giving::Given;
}

void QcServerBarter::take(QcCreature &mob, const QcBarterDefinition &barter, const QcItemStack &stack, quint32 from)
{
    mob.barter->admiring = stack;
    mob.barter->admireSeconds = barter.admireSeconds;
    mob.barter->from = from;
    mob.barter->sought = 0;
    mob.path.clear();
    // Kept with the world from now on, as in the role model (decision E-1081).
    if (!mob.persistent) {
        m_state.entities().creatures().keep(mob, true);
    }
}

void QcServerBarter::alarm(const QcServerClient &client, const QString &block, bool opened)
{
    const QcVec3 eyes = eyesOf(client);
    for (QcCreature &mob : m_state.entities().creatures().editableMobs()) {
        const QcBarterDefinition *barter = barterOf(m_state.content(), mob);
        if (!barter || mob.dormant || mob.isYoung() || !barter->guards.contains(block)
            || (mob.motion.position - client.motion.position).length() > barter->alarmRange) {
            continue;
        }
        const QcMobDefinition &definition = m_state.content().mobs()[mob.type];
        const QcVec3 mobEyes = mob.motion.position + QcVec3{0.0, definition.height * QcMobSteering::eyeShare, 0.0};
        if (opened && !QcLineOfSight::seesPoint(m_state.world(), mobEyes, eyes)) {
            continue;
        }
        // As if he had struck it: the neutral brain turns on him when it next thinks.
        mob.struckBy = client.id;
    }
}

void QcServerBarter::tick()
{
    const double dt = QcPhysics::tickDuration;
    const quint64 tick = m_state.clock().tick();
    for (QcCreature &mob : m_state.entities().creatures().editableMobs()) {
        const QcBarterDefinition *barter = barterOf(m_state.content(), mob);
        if (!barter || mob.dormant) {
            continue;
        }
        if (mob.barter->admires()) {
            mob.barter->admireSeconds -= dt;
            if (mob.barter->admireSeconds <= 0.0) {
                finish(mob, *barter);
            }
        } else if ((tick + mob.id) % seekTicks == 0 && barter->seekRange > 0.0 && mayTake(mob)) {
            seek(mob, *barter);
        }
    }
}

void QcServerBarter::finish(QcCreature &mob, const QcBarterDefinition &barter)
{
    const QcItemStack item = std::exchange(mob.barter->admiring, QcItemStack{});
    const quint32 from = std::exchange(mob.barter->from, 0);
    mob.barter->admireSeconds = 0.0;
    const QcContentRegistry &content = m_state.content();
    // Anger ended the looking: the item falls, nothing is bartered.
    if (mob.provokedBy != 0) {
        throwOut(mob, {item}, 0);
        return;
    }
    if (content.item(item.item).name == barter.currency) {
        throwOut(mob, QcContentLoot::draw(content, barter.table, m_state.random().generate64()), from);
        m_sounds.play(content.mobs()[mob.type].ambientSound, mob.motion.position);
        noteBarter(mob, barter, from);
        return;
    }
    mob.barter->hoard.append(item);
}

void QcServerBarter::noteBarter(const QcCreature &mob, const QcBarterDefinition &barter, quint32 from)
{
    if (from == 0) {
        return;
    }
    const auto &clients = m_state.allClients();
    const auto giver = std::ranges::find_if(clients, [from](const auto &client) { return client->id == from; });
    if (giver != clients.end()) {
        const QString &kind = m_state.content().mobs()[mob.type].name;
        m_state.advancements().trigger(**giver, {.trigger = QcAdvancementTrigger::Bartered, .item = barter.currency, .entity = kind});
    }
}

const QcItemEntity *QcServerBarter::nearestLoved(const QcCreature &mob, const QcBarterDefinition &barter) const
{
    const QcItemEntity *nearest = nullptr;
    double best = std::numeric_limits<double>::max();
    for (const QcItemEntity &item : m_state.entities().items().items()) {
        const QcVec3 offset = item.motion.position - mob.motion.position;
        const double distance = offset.length();
        if (item.surplus || item.pickupDelay > 0.0 || distance > barter.seekRange || distance >= best || std::abs(offset.y) > 3.0
            || !loves(m_state.content(), barter, item.stack.item) || !hasRoomFor(m_state.content(), mob, barter, item.stack.item)
            || mob.barter->ignored.contains(item.id)) {
            continue;
        }
        nearest = &item;
        best = distance;
    }
    return nearest;
}

void QcServerBarter::seek(QcCreature &mob, const QcBarterDefinition &barter)
{
    QcBarterState &state = *mob.barter;
    const quint64 tick = m_state.clock().tick();
    state.ignored.removeIf([tick](QHash<quint32, quint64>::iterator entry) { return entry.value() <= tick; });
    const QcItemEntity *nearest = nearestLoved(mob, barter);
    if (!nearest) {
        state.sought = 0;
        return;
    }
    const QcVec3 towards = nearest->motion.position - mob.motion.position;
    if (towards.length() <= pickupReach) {
        const QcItemStack stack = nearest->stack.withCount(1);
        if (m_state.entities().items().takeOne(nearest->id)) {
            take(mob, barter, stack, 0);
        }
        return;
    }
    if (state.sought != nearest->id) {
        state.sought = nearest->id;
        state.soughtSince = tick;
    }
    // Behind glass or across a gap: left alone for a while, so it does not walk at it for ever.
    if (tick - state.soughtSince >= seekLimitTicks) {
        state.ignored.insert(nearest->id, tick + ignoreTicks);
        state.sought = 0;
        mob.path.clear();
        return;
    }
    // Towards it as the goal of its wandering, until its next look (like partners in love).
    mob.wanderDirection = QcVec3{towards.x, 0.0, towards.z}.normalized();
    QcMobNavigator::setGoal(mob, QcMobPath::Purpose::Wander, nearest->motion.position, 0);
    mob.wanderTimer = seekTicks * QcPhysics::tickDuration * 2.0;
}

void QcServerBarter::throwOut(const QcCreature &mob, const QList<QcItemStack> &stacks, quint32 towards)
{
    const QcMobDefinition &definition = m_state.content().mobs()[mob.type];
    const double yaw = mob.yaw * std::numbers::pi / 180.0;
    QcVec3 direction{-std::sin(yaw), 0.0, -std::cos(yaw)};
    for (const auto &client : m_state.clients()) {
        if (client->id == towards && towards != 0) {
            const QcVec3 offset = client->motion.position - mob.motion.position;
            if (QcVec3{offset.x, 0.0, offset.z}.length() > 1e-6) {
                direction = QcVec3{offset.x, 0.0, offset.z}.normalized();
            }
        }
    }
    const QcVec3 from = mob.motion.position + QcVec3{0.0, definition.height * 0.6, 0.0} + direction * (definition.width * 0.5 + 0.2);
    for (const QcItemStack &stack : stacks) {
        if (!stack.isEmpty()) {
            m_state.entities().items().spawn({.stack = stack, .position = from, .velocity = direction * throwSpeed + QcVec3{0.0, throwLift, 0.0}},
                                             m_state.entities().newId());
        }
    }
}

QList<QcItemStack> QcServerBarter::dropsOf(const QcBarterState &barter)
{
    QList<QcItemStack> held = barter.hoard;
    held.append(barter.admiring);
    held.removeIf([](const QcItemStack &stack) { return stack.isEmpty(); });
    return held;
}

void QcServerBarter::dropHeld(const QcCreature &mob)
{
    if (!mob.barter) {
        return;
    }
    const QcMobDefinition &definition = m_state.content().mobs()[mob.type];
    const QcVec3 centre = mob.motion.position + QcVec3{0.0, definition.height * 0.5, 0.0};
    const QList<QcItemStack> stacks = dropsOf(*mob.barter);
    for (const QcItemStack &stack : stacks) {
        m_state.entities().items().spawnScattered(stack, centre, m_state.entities().newId());
    }
}
