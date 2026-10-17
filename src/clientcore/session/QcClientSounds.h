#pragma once

#include "clientcore/sound/QcAmbience.h"
#include "clientcore/sound/QcLocalSounds.h"
#include "clientcore/sound/QcSoundCue.h"

#include <QList>

#include <functional>

class QcBlockAccess;

// The sounds of a client session: the cues of the last moments (oldest first, at most
// recentLimit), the player's own steps and the ambience loops around him. Plays nothing itself;
// QcClientSession hands the cues on with its soundCue signal.
class QcClientSounds
{
public:
    static constexpr qsizetype recentLimit = 64;
    // The ambience targets are sampled every this many ticks.
    static constexpr int ambienceTicks = 10;

    // Stamps the cue with `tick` and keeps it among the recent ones; false for a cue without a key.
    bool record(QcSoundCue &cue, quint64 tick);
    const QList<QcSoundCue> &recent() const { return m_recent; }
    void clearRecent() { m_recent.clear(); }

    // Fades the ambience levels towards the surroundings of `eye` with rain of strength `rain`
    // falling there, sampled again every ambienceTicks; then nearby lava may pop (a cue for `play`).
    void updateAmbience(const QcBlockAccess &world, const QcVec3 &eye, double rain, quint64 tick,
                        const std::function<void(const QcSoundCue &)> &play);
    const QcAmbience::Levels &ambience() const { return m_ambience; }

    // The player's steps, landings and swimming.
    QcLocalSounds &local() { return m_local; }

private:
    QList<QcSoundCue> m_recent;
    QcLocalSounds m_local;
    QcAmbience::Levels m_ambience;
    QcAmbience::Levels m_ambienceTarget;
};
