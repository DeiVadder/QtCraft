#include "server/villages/QcServerVillagers.h"

#include "server/blocks/QcOpeners.h"
#include "server/blocks/QcServerBlocks.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"

#include <algorithm>

using namespace Qt::StringLiterals;

QcServerVillagers::QcServerVillagers(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks)
    : m_state(state)
    , m_sounds(sounds)
    , m_blocks(blocks)
{
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
    if (open && villager->villager.openedDoor == action.pos) {
        villager->villager.openedDoor.reset();
    } else if (!open && !villager->villager.openedDoor) {
        villager->villager.openedDoor = action.pos;
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

    for (QcCreature &mob : creatures.editableMobs()) {
        if (mob.villager.lookChanged) {
            mob.villager.lookChanged = false;
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
    const QcContentRegistry &content = m_state.content();
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    const QcBlockDefinition *crop = block ? &content.block(*block) : nullptr;
    const QcBlockProperty *stages = crop && crop->crop.isCrop() ? crop->property(crop->crop.property) : nullptr;
    if (!block || !stages || stages->values.isEmpty() || content.property(*block, crop->crop.property) != stages->values.constLast()) {
        return;
    }

    m_blocks.setAndBroadcast(pos, content.withProperty(*block, crop->crop.property, stages->values.constFirst()));
    m_sounds.playBlock(*crop, QcBlockSoundAction::Break, pos);
    if (QcCreature *farmer = m_state.entities().creatures().find(villager)) {
        farmer->villager.produce = std::min(farmer->villager.produce + 1, QcVillagerRecord::maxProduce);
        farmer->attackedThisTick = true;
    }
}
