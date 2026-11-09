// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/session/QcSessionAudio.h"

#include "audio/QcAudioEngine.h"
#include "clientcore/session/QcClientSession.h"
#include "common/physics/QcPhysics.h"

QcSessionAudio::QcSessionAudio(QcAudioEngine &audio)
    : m_audio(audio)
{
}

void QcSessionAudio::connectSession(const QcClientSession &session)
{
    connect(&session, &QcClientSession::ticked, this, [this, &session] {
        m_audio.setBiomeAmbience(session.sounds().biomeLoop(), session.sounds().biomeLevel());
        m_audio.setAmbience(session.sounds().ambience());
        // The music of the biome the player stands in, from its next piece on (decision E-1123).
        m_audio.setMusicStyle(session.worldMirror().atmosphere().current().music.value_or(QcMusicStyle{}));
        QcMusicDirector &director = m_audio.music().director();
        director.setSituation(musicSituationOf(session, director.library().get()));
        m_audio.moveListener(listenerOf(session));
    });
    connect(&session, &QcClientSession::soundCue, this, [this, &session](const QcSoundCue &cue) { m_audio.play(cue, listenerOf(session)); });
}

QcMusicDirector::Situation QcSessionAudio::musicSituationOf(const QcClientSession &session, const QcMusicLibrary *library)
{
    const QcWorldMirror &world = session.worldMirror();
    const QString biome = library ? library->biomeOfLoop(world.atmosphere().current().loop) : QString();
    return QcMusicDirector::world(world.dimension().name, biome, world.worldTime(), session.sounds().ambience().cave);
}

QcSoundSpatial::Listener QcSessionAudio::listenerOf(const QcClientSession &session)
{
    const QcVec3 ears = session.playerMotion().position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    // Left and right as seen on screen: the front view looks back at the player.
    const bool front = session.cameraMode() == QcCameraMode::Front;
    return {ears, session.intent().yaw + (front ? 180.0 : 0.0)};
}
