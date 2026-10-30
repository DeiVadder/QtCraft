// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "audio/QcAudioOutput.h"
#include "clientcore/sound/QcAmbience.h"
#include "clientcore/sound/QcAudioMixer.h"
#include "clientcore/sound/QcPositionalVoices.h"
#include "clientcore/sound/QcSoundBank.h"
#include "clientcore/sound/QcSoundCue.h"
#include "clientcore/sound/QcSoundSpatial.h"
#include "common/content/registry/QcContentRegistry.h"

#include <QFutureWatcher>
#include <QObject>
#include <QRandomGenerator>
#include <QSet>
#include <QTimer>

#include <array>
#include <memory>

/*! \brief Everything the graphical client hears: sound cues placed around the listener, UI sounds,
    the four ambience loops and the background music.

    Lives in the GUI thread; rendering the sound bank and music pieces happens on worker threads, so
    no frame ever waits for synthesis. */
class QcAudioEngine : public QObject
{
    Q_OBJECT

public:
    /*! Pause between music pieces, and before the first one, in seconds. \qcshared */
    static constexpr int minMusicPause = 60;
    static constexpr int maxMusicPause = 180;
    static constexpr int firstMusicDelay = 15;
    /*! Quieter voices are not worth a mixer voice. */
    static constexpr float audibleGain = 0.002f;
    /*! Level of the music relative to its bus volume. */
    static constexpr float musicGain = 0.6f;

    /*! A silent engine, child of \a parent; start() opens the output. */
    explicit QcAudioEngine(QObject *parent = nullptr);
    ~QcAudioEngine() override;
    Q_DISABLE_COPY_MOVE(QcAudioEngine)

    /*! Opens the output device; with \a enabled false no device is touched and the game is
        silent. */
    void start(bool enabled);
    /*! Returns whether an output device plays. */
    bool isActive() const { return m_output.isActive(); }
    /*! Returns the output (device, mixer). */
    QcAudioOutput &output() { return m_output; }

    /*! Renders the sounds of \a content in the background; until that is done cues are skipped. */
    void setContent(std::shared_ptr<const QcContentRegistry> content);
    /*! Returns the content of the sounds, null before setContent(). */
    const QcContentRegistry *content() const { return m_content.get(); }
    /*! Returns whether the sound bank of the content is rendered (bankReady()). */
    bool isBankReady() const { return m_bank != nullptr; }
    /*! Sets the volumes of the buses, each from 0 to 1: \a master over all, \a effects, \a music
        and \a ambient; \a muted silences everything. */
    void setVolumes(double master, double effects, double music, double ambient, bool muted);

    /*! Plays \a cue as \a listener hears it; its voice follows the listener until it ends
        (moveListener). */
    void play(const QcSoundCue &cue, const QcSoundSpatial::Listener &listener);
    /*! Places the playing positional voices anew for where \a listener is now (every tick). */
    void moveListener(const QcSoundSpatial::Listener &listener);
    /*! Returns where moveListener() placed the listener last (also while the output is
        inactive). */
    const QcSoundSpatial::Listener &listener() const { return m_listener; }
    /*! Plays the sound \a key without a position (menu clicks). */
    void playUi(const QString &key);
    /*! Sets the \a levels of the ambience loops; a loop starts with its first audible level. */
    void setAmbience(QcAmbience::Levels levels);
    /*! Background music plays while \a enabled (in a world), with pauses between pieces. */
    void setMusicEnabled(bool enabled);
    /*! Stops ambience, music and effects (leaving a world). */
    void stopWorldSounds();

signals:
    /*! The sound bank of the content is rendered; cues play from now on. */
    void bankReady();

private:
    void buildBank();
    // The output runs a fresh mixer at a new rate: rebuilds what was rendered for the old one.
    void mixerReset();
    std::shared_ptr<const QcSampleBuffer> pickVariant(const QcSoundBank::Entry &entry);
    QcAudioMixer::VoiceId startVoice(const QcSoundBank::Entry &entry, const QcAudioMixer::PlayParameters &parameters);
    void scheduleMusic(int seconds);
    void renderMusic();
    void warnUnknown(const QString &key);

    QcAudioOutput m_output;
    std::shared_ptr<const QcContentRegistry> m_content;
    std::shared_ptr<const QcSoundBank> m_bank;
    QFutureWatcher<std::shared_ptr<const QcSoundBank>> m_bankWatcher;
    QFutureWatcher<std::shared_ptr<const QcSampleBuffer>> m_musicWatcher;
    QTimer m_musicTimer;
    bool m_musicEnabled = false;
    QcAudioMixer::VoiceId m_musicVoice = 0;
    std::array<QcAudioMixer::VoiceId, 4> m_ambienceVoices{};
    QcAmbience::Levels m_ambience;
    QcPositionalVoices m_positional;
    QcSoundSpatial::Listener m_listener;
    QRandomGenerator m_random;
    QSet<QString> m_warned;
};
