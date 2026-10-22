#pragma once

#include "common/gameplay/QcStatusEffects.h"

#include <QtGlobal>

class QcContentRegistry;
class QcMobManager;
class QcPlayerHealth;
class QcServerSounds;
class QcServerState;
struct QcServerClient;

// The status effects of players and creatures in the server tick (decision E-450): every effect
// counts down and does what its kind (QcEffectDefinition) says at its level; poison hurts every
// QcEffectDefinition::periodAt ticks without taking the health below its minHealth, players through
// QcPlayerHealth::damage (unblockable), creatures through QcServerCombat::hurtCreature (no push,
// QcMobManager::damage); hunger exhausts players' food (decision E-838; slowness acts where bodies
// move, QcStatusEffects::speedFactor). Dead players lose their effects. A player hears of his own with his health
// (QcMsg::PlayerHealth::effects) whenever they change and every second while he has any, so the HUD
// shows the time left.
class QcServerEffects
{
public:
    // A player with effects hears of them at least this often.
    static constexpr int sendIntervalTicks = 20;

    QcServerEffects(QcServerState &state, QcServerSounds &sounds, QcPlayerHealth &health);

    // One simulated tick of the effects of every playing player and awake creature.
    void tick();

    // Gives a creature an effect of a known kind it is not immune to; false when nothing changed.
    static bool giveCreature(const QcContentRegistry &content, QcMobManager &creatures, quint32 creature, const QcStatusEffect &effect);
    // Gives a living player an effect of a known kind; false when nothing changed.
    static bool givePlayer(const QcContentRegistry &content, QcServerClient &client, const QcStatusEffect &effect);

private:
    void tickPlayer(QcServerClient &client);
    void tickCreatures();

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcPlayerHealth &m_health;
};
