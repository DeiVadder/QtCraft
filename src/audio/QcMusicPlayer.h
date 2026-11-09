// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/music/QcMusicDirector.h"
#include "clientcore/sound/QcAudioMixer.h"
#include "common/core/QcResult.h"
#include "common/world/QcAtmosphere.h"

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QObject>
#include <QTimer>

#include <memory>

class QcAudioOutput;

/*! \brief Plays the background music on the music bus as QcMusicDirector decides (decision
    E-1137): tracks of the content packs streamed from their Ogg Vorbis files (QcVorbisStream,
    opened on a worker thread, decoded on the audio thread), or pieces of QcMusicComposer rendered
    on a worker thread.

    Lives in the GUI thread. A stream is released here, after its voice left the mixer, so that it
    is never freed on the audio thread. */
class QcMusicPlayer : public QObject
{
    Q_OBJECT

public:
    /*! Level of the music relative to its bus volume. */
    static constexpr float musicGain = 0.6f;
    /*! How often the director's time advances. */
    static constexpr int tickMilliseconds = 250;

    /*! A silent player on \a output, child of \a parent. */
    explicit QcMusicPlayer(QcAudioOutput &output, QObject *parent = nullptr);
    ~QcMusicPlayer() override;
    Q_DISABLE_COPY_MOVE(QcMusicPlayer)

    /*! Returns the director (where the player is, the source, the tracks). */
    [[nodiscard]] QcMusicDirector &director() { return m_director; }
    /*! Returns the director, read-only. */
    [[nodiscard]] const QcMusicDirector &director() const { return m_director; }
    /*! Composed pieces from the next one on follow \a style (a biome's music, decision E-1123). */
    void setStyle(const QcMusicStyle &style) { m_style = style; }
    /*! Music plays while \a enabled, with pauses between pieces; off stops the piece at once. */
    void setEnabled(bool enabled);
    /*! The mixer's voices were stopped or the mixer was replaced: the piece is gone, the next one
        follows after the first delay. */
    void interrupted();
    /*! Returns the mixer's voice of the piece playing (0: none). */
    [[nodiscard]] QcAudioMixer::VoiceId voice() const { return m_voice; }
    /*! Lets \a seconds pass for the director and does what it says (the timer calls it every
        tickMilliseconds). */
    void advance(double seconds);

private:
    /*! \brief A track opened on a worker thread. \qcfields */
    struct OpenedTrack
    {
        std::shared_ptr<QcStreamCursor> cursor;
        double seconds = 0.0;
    };

    void playTrack(const QString &id);
    void playGenerated();
    void fadeOut();
    // Stops the voice and drops its stream here, on the GUI thread.
    void release();
    void trackOpened();
    void pieceRendered();
    // The mixer gave `played` (0: none, it is full or stopped) for a piece of `seconds`: it plays
    // on if the director agrees, else the piece is dropped.
    void begin(QcAudioMixer::VoiceId played, double seconds);
    void tick();
    void setVoice(QcAudioMixer::VoiceId voice) { m_voice = voice; }
    [[nodiscard]] const std::shared_ptr<QcStreamCursor> &stream() const { return m_stream; }
    void setStream(std::shared_ptr<QcStreamCursor> stream) { m_stream = std::move(stream); }

    QcAudioOutput &m_output;
    QcMusicDirector m_director;
    QcMusicStyle m_style;
    QTimer m_timer;
    QElapsedTimer m_clock;
    QFutureWatcher<QcResult<OpenedTrack>> m_trackWatcher;
    QFutureWatcher<std::shared_ptr<const QcSampleBuffer>> m_pieceWatcher;
    QcAudioMixer::VoiceId m_voice = 0;
    std::shared_ptr<QcStreamCursor> m_stream;
};
