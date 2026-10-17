#pragma once

#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"

class QcFireIgnition;
class QcPlayerHealth;
class QcServerSounds;
class QcServerState;

// What a lightning strike does to the world (decision E-396), the strike hook of QcServerWeather.
// Players and creatures near the struck cell take the content's lightning damage (players through
// their armour, like the contact damage of a block) and catch fire; players in creative mode are
// spared. Fire comes only through QcFireIgnition::lightning: a few sparks of their own, each within
// the guarantees of fire (decision E-392), none while fire does not spread. The flash and the
// thunder the players already got from QcServerWeather::strike.
class QcLightningStrike
{
public:
    // Counts since the server started (tests, measurements).
    struct Numbers
    {
        quint64 strikes = 0;
        quint64 playersHit = 0;
        quint64 creaturesHit = 0;
        quint64 fires = 0;
    };

    // Bodies this many blocks above the struck cell are still hit (a tall creature on the ground).
    static constexpr int reachUp = 6;

    QcLightningStrike(QcServerState &state, QcServerSounds &sounds, QcPlayerHealth &health, QcFireIgnition &ignition);

    // Lightning struck the cell `pos` (the cell above a column's top).
    void strike(QcBlockPos pos);
    const Numbers &numbers() const { return m_numbers; }

private:
    // Whether a body with its feet at `feet` stands within reach of the strike at `pos`.
    bool withinReach(QcBlockPos pos, const QcVec3 &feet) const;
    void hitPlayers(QcBlockPos pos);
    void hitCreatures(QcBlockPos pos);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcPlayerHealth &m_health;
    QcFireIgnition &m_ignition;
    Numbers m_numbers;
};
