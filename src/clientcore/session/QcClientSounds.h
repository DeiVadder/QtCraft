// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/sound/QcAmbience.h"
#include "clientcore/sound/QcLocalSounds.h"
#include "clientcore/sound/QcSoundCue.h"
#include "common/world/QcAtmosphere.h"

#include <QList>

#include <functional>
#include <optional>

class QcBlockAccess;

/*! \brief The sounds of a client session: the cues of the last moments (oldest first, at most
    recentLimit), the player's own steps and the ambience loops around them.

    Plays nothing itself; QcClientSession hands the cues on with its soundCue signal. */
class QcClientSounds
{
public:
    /*! Most cues kept in recent(). */
    static constexpr qsizetype recentLimit = 64;
    /*! The ambience targets are sampled every this many ticks. */
    static constexpr int ambienceTicks = 10;

    /*! Keeps \a cue stamped with \a tick among the recent ones and returns it; none for a cue
        without a key. */
    [[nodiscard]] std::optional<QcSoundCue> record(QcSoundCue cue, quint64 tick);
    /*! Returns the cues of the last moments, oldest first. */
    const QList<QcSoundCue> &recent() const { return m_recent; }
    /*! Forgets the recent cues. */
    void clearRecent() { m_recent.clear(); }

    /*! Fades the ambience levels towards the surroundings of \a eye in \a world with rain of
        strength \a rain falling there, sampled again every ambienceTicks (by the client
        \a tick); then nearby lava may pop (a cue for \a play). */
    void updateAmbience(const QcBlockAccess &world, const QcVec3 &eye, double rain, quint64 tick,
                        const std::function<void(const QcSoundCue &)> &play);
    /*! Returns the ambience levels as faded so far. */
    const QcAmbience::Levels &ambience() const { return m_ambience; }

    /*! Distance in metres from the listener at which the sounds of a biome now and then play. */
    static constexpr double additionsDistance = 6.0;
    /*! One client \a tick of the ambience \a atmosphere of the biome the listener at \a eye
        stands in (decision E-1122): its loop fades in (QcAmbience::fadePerSecond), another
        biome's loop fades out before the new one fades in; the biome's sound now and then plays
        (a cue for \a play) with its chance per tick and in a random direction, both drawn from
        the tick (splitmix64), so a session plays the same sounds at the same ticks. */
    void updateBiomeAmbience(const QcBiomeAtmosphere &atmosphere, const QcVec3 &eye, quint64 tick,
                             const std::function<void(const QcSoundCue &)> &play);
    /*! Returns the loop of the biome ambience that sounds. */
    const QString &biomeLoop() const { return m_biomeLoop; }
    /*! Returns the level of the biome ambience (0..1). */
    float biomeLevel() const { return m_biomeLevel; }

    /*! Returns the player's steps, landings and swimming. */
    QcLocalSounds &local() { return m_local; }

private:
    QList<QcSoundCue> m_recent;
    QcLocalSounds m_local;
    QcAmbience::Levels m_ambience;
    QcAmbience::Levels m_ambienceTarget;
    QString m_biomeLoop;
    float m_biomeLevel = 0.0f;

    void setBiomeLoop(const QString &loop) { m_biomeLoop = loop; }
    void setBiomeLevel(float level) { m_biomeLevel = level; }
};
