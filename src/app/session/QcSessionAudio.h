#pragma once

#include "clientcore/sound/QcSoundSpatial.h"

#include <QObject>

class QcAudioEngine;
class QcClientSession;

// What a client session plays (decision E-876), part of QcSessionWiring: its sound cues, and each
// tick its ambience levels and the listener, so that playing positional voices follow the player.
// Everything is heard from listenerOf(session). Lives in the GUI thread; the engine outlives it.
class QcSessionAudio : public QObject
{
    Q_OBJECT

public:
    explicit QcSessionAudio(QcAudioEngine &audio);

    // Plays the sounds of `session` from now on (until the session goes).
    void connectSession(const QcClientSession &session);

    // Where the player of `session` hears from now: the eyes, looking the way the screen shows.
    [[nodiscard]] static QcSoundSpatial::Listener listenerOf(const QcClientSession &session);

private:
    QcAudioEngine &m_audio;
};
