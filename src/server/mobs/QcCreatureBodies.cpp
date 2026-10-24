#include "server/mobs/QcCreatureBodies.h"

QcVec3 QcCreatureBodies::pushOnPlayer(const QcVec3 &feet, const QList<QcCreature> &mobs, const QList<QcMobDefinition> &definitions,
                                      quint64 tick, quint64 generation)
{
    if (tick != m_tick || generation != m_generation) {
        build(mobs, definitions);
        m_tick = tick;
        m_generation = generation;
    }
    return QcBodyPush::playerVelocity(m_grid.sum(QcBodyPush::player(feet)));
}

void QcCreatureBodies::build(const QList<QcCreature> &mobs, const QList<QcMobDefinition> &definitions)
{
    m_grid.clear();
    for (const QcCreature &mob : mobs) {
        // Resting creatures are far from every player and not shown to them (decision E-372).
        if (mob.dormant || mob.type < 0 || mob.type >= definitions.size()) {
            continue;
        }
        const QcMobDefinition &definition = definitions[mob.type];
        if (QcBodyPush::pushesPlayers(definition, mob.pose)) {
            m_grid.add(QcBodyPush::creature(definition, mob.motion.position));
        }
    }
    m_grid.finish();
}
