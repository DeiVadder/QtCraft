// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/villages/QcServerVillagers.h"

#include "server/blocks/QcOpeners.h"
#include "server/blocks/QcServerBlocks.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

QcServerVillagers::QcServerVillagers(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks)
    : m_state(state)
    , m_sounds(sounds)
    , m_blocks(blocks)
{
    const QcContentRegistry &content = m_state.content();
    for (int type = 0; type < content.mobs().size(); ++type) {
        const QcVillagerSettings &species = content.villagerSettingsOf(type);
        QHash<QcItemId, int> &worth = m_worth[type];
        for (auto it = species.breeding.foods.cbegin(); it != species.breeding.foods.cend(); ++it) {
            if (const std::optional<QcItemId> item = content.itemId(it.key()); item && it.value() > 0) {
                worth.insert(*item, it.value());
                m_foods.insert(*item);
            }
        }
    }
}

void QcServerVillagers::door(const QcVillagerBehaviour::Action &action)
{
    const bool open = action.kind == QcVillagerBehaviour::Action::Kind::OpenDoor;
    if (setDoor(action.pos, open)) {
        return;
    }

    // A body in the door's way: the villager asks again on its next step to open it, or tries again
    // to close it on its next steps.
    QcCreature *villager = m_state.entities().creatures().find(action.villager);
    if (!villager) {
        return;
    }
    if (open && villager->villager->openedDoor == action.pos) {
        villager->villager->openedDoor.reset();
    } else if (!open && !villager->villager->openedDoor) {
        villager->villager->openedDoor = action.pos;
    }
}

void QcServerVillagers::apply()
{
    QcMobManager &creatures = m_state.entities().creatures();
    const QList<QcVillagerBehaviour::Action> actions = creatures.villagers().takeActions();
    for (const QcVillagerBehaviour::Action &action : actions) {
        switch (action.kind) {
        case QcVillagerBehaviour::Action::Kind::OpenDoor:
        case QcVillagerBehaviour::Action::Kind::CloseDoor:
            door(action);
            break;
        case QcVillagerBehaviour::Action::Kind::Harvest:
            harvest(action.villager, action.pos);
            break;
        }
    }

    if (++m_foodTick >= foodTicks) {
        m_foodTick = 0;
        pickUpFood();
    }

    for (QcCreature &mob : creatures.editableMobs()) {
        if (mob.isVillager() && mob.villager->lookChanged) {
            mob.villager->lookChanged = false;
            m_state.entities().markStale(mob.id);
        }
    }
}

bool QcServerVillagers::setDoor(QcBlockPos pos, bool open)
{
    const QcContentRegistry &content = m_state.content();
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    // Only doors a hand opens (wooden ones): iron doors keep to their signal.
    if (!block || !content.block(*block).toggles || !content.block(*block).property(u"open"_s)) {
        return true;
    }
    if ((content.property(*block, u"open"_s) == u"true"_s) == open) {
        return true;
    }
    return QcOpeners(m_state, m_sounds, m_blocks).toggle(pos, *block);
}

void QcServerVillagers::harvest(quint32 villager, QcBlockPos pos)
{
    // Without the game rule mobGriefing villagers leave crops alone, like in the reference game, and a
    // confined one or one without a mind always (QcCreature::mayGrief, decision E-510).
    QcCreature *farmer = m_state.entities().creatures().find(villager);
    if (!farmer || !farmer->isVillager() || !farmer->mayGrief(m_state.info().mobGriefing)) {
        return;
    }
    const QcContentRegistry &content = m_state.content();
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    const QcBlockDefinition *crop = block ? &content.block(*block) : nullptr;
    const QcBlockProperty *stages = crop && crop->crop.isCrop() ? crop->property(crop->crop.property) : nullptr;
    if (!block || !stages || stages->values.isEmpty() || content.property(*block, crop->crop.property) != stages->values.constLast()) {
        return;
    }

    m_blocks.setAndBroadcast(pos, content.withProperty(*block, crop->crop.property, stages->values.constFirst()));
    m_sounds.playBlock(*crop, QcBlockSoundAction::Break, pos);
    farmer->villager->produce = std::min(farmer->villager->produce + 1, QcVillagerRecord::maxProduce);
    farmer->attackedThisTick = true;
}

void QcServerVillagers::pickUpFood()
{
    // Without the game rule mobGriefing villagers pick nothing up, like in the reference game.
    if (!m_state.info().mobGriefing) {
        return;
    }

    // The items lying in the world that some villager eats, gathered first: mostly there are none.
    QcItemEntityManager &items = m_state.entities().items();
    struct Food
    {
        quint32 id = 0;
        QcVec3 at;
        QcItemId item = 0;
        int count = 0;
    };
    QList<Food> lying;
    for (const QcItemEntity &item : items.items()) {
        if (!item.surplus && item.pickupDelay <= 0.0 && !item.stack.isEmpty() && m_foods.contains(item.stack.item)) {
            lying.append({item.id, item.motion.position, item.stack.item, item.stack.count});
        }
    }
    if (lying.isEmpty()) {
        return;
    }

    for (QcCreature &mob : m_state.entities().creatures().editableMobs()) {
        // A confined villager or one without a mind picks nothing up (QcCreature::mayGrief).
        if (mob.dormant || !mob.isVillager() || mob.isYoung() || mob.pose == QcPose::Lying || !mob.mayGrief(m_state.info().mobGriefing)) {
            continue;
        }
        const QcVillagerBreedingSettings &breeding = m_state.content().villagerSettingsOf(mob.type).breeding;
        const int wants = breeding.shareAbove + breeding.food;
        const QHash<QcItemId, int> worths = m_worth.value(mob.type);
        for (Food &food : lying) {
            const int worth = worths.value(food.item);
            const QcVec3 offset = food.at - mob.motion.position;
            const bool near = std::hypot(offset.x, offset.z) <= foodReach && std::abs(offset.y) <= 1.0;
            if (food.count <= 0 || worth <= 0 || !near || mob.villager->produce >= wants) {
                continue;
            }
            const int taken = std::min(food.count, (wants - mob.villager->produce + worth - 1) / worth);
            if (items.take(food.id, taken)) {
                food.count -= taken;
                mob.villager->produce = std::min(mob.villager->produce + taken * worth, QcVillagerRecord::maxProduce);
            }
        }
    }
}
