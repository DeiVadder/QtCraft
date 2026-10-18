#include "server/mobs/QcCreatureTraits.h"

#include "server/blocks/QcBlockRules.h"
#include "server/blocks/QcServerBlocks.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/gameplay/QcServerCombat.h"

QcCreatureTraits::QcCreatureTraits(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks)
    : m_state(state)
    , m_sounds(sounds)
    , m_blocks(blocks)
{
    m_departures = m_state.entities().creatures().onDeparture([this](const QcCreature &mob, QcMobManager::Departure why) {
        if (why == QcMobManager::Departure::Died) {
            dropCarried(mob);
        }
    });
}

QcCreatureTraits::~QcCreatureTraits()
{
    m_state.entities().creatures().removeDepartureListener(m_departures);
}

void QcCreatureTraits::apply()
{
    QcMobManager &creatures = m_state.entities().creatures();
    const QList<QcMobManager::BlockAction> actions = creatures.takeBlockActions();
    for (const QcMobManager::BlockAction &action : actions) {
        switch (action.kind) {
        case QcMobManager::BlockAction::Kind::Take:
            take(action);
            break;
        case QcMobManager::BlockAction::Kind::Place:
            place(action);
            break;
        case QcMobManager::BlockAction::Kind::Drop:
            drop(action);
            break;
        }
    }
    announceTeleports();
}

void QcCreatureTraits::take(const QcMobManager::BlockAction &action)
{
    QcMobManager &creatures = m_state.entities().creatures();
    QcCreature *mob = creatures.find(action.mob);
    // Two creatures may have chosen the same block in one tick: the first takes it.
    if (!mob || mob->carried != qcAirBlockId || !mob->mayGrief(m_state.info().mobGriefing) || m_state.world().blockAt(action.pos) != action.block) {
        return;
    }

    m_blocks.breakBlock(action.pos, false);
    if (m_state.world().blockAt(action.pos) == action.block) {
        return;
    }
    creatures.carryBlock(*mob, action.block);
    // Kept with the world while it carries the block, unless it was kept already.
    if (!mob->persistent) {
        creatures.keep(*mob, true);
        mob->keptForBlock = true;
    }
}

void QcCreatureTraits::place(const QcMobManager::BlockAction &action)
{
    QcMobManager &creatures = m_state.entities().creatures();
    QcCreature *mob = creatures.find(action.mob);
    if (!mob || mob->carried != action.block || !mob->mayGrief(m_state.info().mobGriefing) || m_state.world().blockAt(action.pos) != qcAirBlockId
        || bodyIn(action.pos) || !QcBlockRules::isSupported(m_state.world(), action.pos, action.block)) {
        return;
    }

    m_blocks.setAndBroadcast(action.pos, action.block);
    if (m_state.world().blockAt(action.pos) != action.block) {
        return;
    }
    letGo(*mob);
}

void QcCreatureTraits::drop(const QcMobManager::BlockAction &action)
{
    QcCreature *mob = m_state.entities().creatures().find(action.mob);
    if (!mob || mob->carried != action.block || mob->mayGrief(m_state.info().mobGriefing)) {
        return;
    }
    dropCarried(*mob);
    letGo(*mob);
}

void QcCreatureTraits::letGo(QcCreature &mob)
{
    QcMobManager &creatures = m_state.entities().creatures();
    creatures.carryBlock(mob, qcAirBlockId);
    if (mob.keptForBlock) {
        creatures.keep(mob, false);
        mob.keptForBlock = false;
    }
}

bool QcCreatureTraits::bodyIn(QcBlockPos pos) const
{
    const QcAabb cell = QcLineOfSight::blockBox(pos);
    const QcEntities &entities = m_state.entities();
    bool inside = false;
    entities.forEachBody([&](const QcEntity &body) {
        const std::optional<QcAabb> box = body.dormant ? std::nullopt : entities.boxOf(body);
        inside = inside || (box && box->intersects(cell));
    });
    return inside;
}

void QcCreatureTraits::announceTeleports()
{
    for (QcCreature &mob : m_state.entities().creatures().editableMobs()) {
        if (!mob.teleportedFrom) {
            continue;
        }

        const QcMobDefinition &definition = m_state.content().mobs()[mob.type];
        if (definition.teleport && !definition.teleport->sound.isEmpty()) {
            const QcVec3 middle{0.0, definition.height * 0.5, 0.0};
            m_sounds.play(definition.teleport->sound, *mob.teleportedFrom + middle);
            m_sounds.play(definition.teleport->sound, mob.motion.position + middle);
        }
        mob.teleportedFrom.reset();
        // Spawned anew on the clients: it appears there instead of sliding across.
        m_state.entities().markStale(mob.id);
    }
}

void QcCreatureTraits::dropCarried(const QcCreature &mob)
{
    if (mob.carried == qcAirBlockId || mob.carriedItem == qcNoItem) {
        return;
    }
    const QcMobDefinition &definition = m_state.content().mobs()[mob.type];
    const QcVec3 centre = mob.motion.position + QcVec3{0.0, definition.height * 0.5, 0.0};
    QcServerCombat::dropLoot(m_state, {QcItemDrop{m_state.content().item(mob.carriedItem).name, 1}}, centre);
}
