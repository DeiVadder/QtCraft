#include "QcServerVillagers.h"

#include "QcOpeners.h"
#include "QcServerBlocks.h"
#include "QcServerCombat.h"
#include "QcServerSounds.h"
#include "QcServerState.h"

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

bool QcServerVillagers::talk(const QcServerClient &client, quint32 id)
{
    QcCreature *mob = m_state.entities().creatures().find(id);
    if (!mob || !m_state.content().mobs()[mob->type].villager) {
        return false;
    }
    const QcMobDefinition &definition = m_state.content().mobs()[mob->type];
    const QcVec3 eye = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const QcVec3 centre = mob->motion.position + QcVec3{0.0, definition.height * 0.5, 0.0};
    if ((centre - eye).length() > QcServerCombat::attackReach + definition.width + 1.0) {
        return false;
    }

    // Trading comes with stage 37.2c; until then the villager turns to the player and greets them.
    mob->villager.talk = QcVillagerBehaviour::talkSeconds;
    mob->villager.talkTowards = client.motion.position;
    if (!definition.ambientSound.isEmpty()) {
        m_sounds.play(definition.ambientSound, centre);
    }
    return true;
}
