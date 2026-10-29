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
        m_audio.setAmbience(session.sounds().ambience());
        m_audio.moveListener(listenerOf(session));
    });
    connect(&session, &QcClientSession::soundCue, this, [this, &session](const QcSoundCue &cue) { m_audio.play(cue, listenerOf(session)); });
}

QcSoundSpatial::Listener QcSessionAudio::listenerOf(const QcClientSession &session)
{
    const QcVec3 ears = session.playerMotion().position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    // Left and right as seen on screen: the front view looks back at the player.
    const bool front = session.cameraMode() == QcCameraMode::Front;
    return {ears, session.intent().yaw + (front ? 180.0 : 0.0)};
}
