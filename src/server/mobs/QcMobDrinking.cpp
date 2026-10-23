#include "server/mobs/QcMobDrinking.h"

#include "common/physics/QcPhysics.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"

#include <algorithm>
#include <cmath>

namespace {

quint64 ticksOf(double seconds)
{
    return static_cast<quint64>(std::max<qint64>(1, std::llround(seconds / QcPhysics::tickDuration)));
}

} // namespace

QcMobDrinking::QcMobDrinking(QcServerState &state, QcServerSounds &sounds)
    : m_state(state)
    , m_sounds(sounds)
{
}

void QcMobDrinking::tick()
{
    const quint64 now = m_state.clock().tick();
    for (QcCreature &mob : m_state.entities().creatures().editableMobs()) {
        const QcMobDefinition &definition = m_state.content().mobs()[mob.type];
        if (!definition.drinks || !mob.archer.has() || mob.dormant || mob.noAi || mob.health <= 0.0) {
            continue;
        }

        const QcDrinkHealDefinition &drink = *definition.drinks;
        QcArcherState &state = *mob.archer;
        if (!state.drinking()) {
            // Begins when a whole drink would heal and the last one is far enough past; the draw ends.
            if (mob.health <= definition.maxHealth - drink.heal && now >= state.drinkReady) {
                state.drinkStart = static_cast<qint64>(now);
                state.drawStart = -1;
                m_state.entities().markStale(mob.id);
            }
            continue;
        }
        if (now - static_cast<quint64>(state.drinkStart) < ticksOf(drink.seconds)) {
            continue;
        }

        mob.health = std::min(definition.maxHealth, mob.health + drink.heal);
        state.drinkStart = -1;
        state.drinkReady = now + ticksOf(drink.cooldownSeconds);
        m_state.entities().markStale(mob.id);
        ++m_drinks;
        if (!drink.sound.isEmpty()) {
            m_sounds.play(drink.sound, mob.motion.position + QcVec3{0.0, definition.height * 0.8, 0.0});
        }
    }
}
