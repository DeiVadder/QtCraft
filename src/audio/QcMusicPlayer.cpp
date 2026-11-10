// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/QcMusicPlayer.h"

#include "audio/QcAudioLogging.h"
#include "audio/QcAudioOutput.h"
#include "clientcore/music/QcVorbisStream.h"
#include "clientcore/sound/QcMusicArrangement.h"
#include "clientcore/sound/QcMusicComposer.h"

#include <QRandomGenerator>
#include <QtConcurrent/QtConcurrentRun>

#include <cmath>

using namespace Qt::StringLiterals;

QcMusicPlayer::QcMusicPlayer(QcAudioOutput &output, QObject *parent)
    : QObject(parent)
    , m_output(output)
    , m_director(QRandomGenerator::global()->generate64())
{
    m_timer.setInterval(tickMilliseconds);
    connect(&m_timer, &QTimer::timeout, this, &QcMusicPlayer::tick);
    connect(&m_trackWatcher, &QFutureWatcherBase::finished, this, &QcMusicPlayer::trackOpened);
    connect(&m_pieceWatcher, &QFutureWatcherBase::finished, this, &QcMusicPlayer::pieceRendered);
}

QcMusicPlayer::~QcMusicPlayer()
{
    // The workers hold only copies of what they need; waiting keeps their results from outliving
    // the watchers. A piece being rendered stops at its next note.
    stopRendering();
    m_trackWatcher.waitForFinished();
    m_pieceWatcher.waitForFinished();
    release();
}

void QcMusicPlayer::setEnabled(bool enabled)
{
    director().setEnabled(enabled);
    if (enabled) {
        m_clock.start();
        m_timer.start();
        return;
    }

    m_timer.stop();
    stopRendering();
    release();
}

void QcMusicPlayer::stopRendering()
{
    m_stopRendering->store(true, std::memory_order_relaxed);
}

void QcMusicPlayer::dropEndedPiece()
{
    if (!piece()) {
        return;
    }
    const QcAudioMixer::VoiceId playing = voice();
    bool ended = true;
    m_output.withMixer([playing, &ended](QcAudioMixer &mixer) { ended = playing == 0 || !mixer.isPlaying(playing); });
    if (ended) {
        setPiece(nullptr);
    }
}

QcMusicArrangement QcMusicPlayer::nextArrangement() const
{
    return m_arrangement ? *m_arrangement : QcMusicArrangement::forPlaces(director().situation().places);
}

void QcMusicPlayer::interrupted()
{
    // The voice is gone with the mixer's: only the stream or piece is left to drop.
    setVoice(0);
    setStream(nullptr);
    setPiece(nullptr);
    director().interrupted();
}

void QcMusicPlayer::tick()
{
    advance(static_cast<double>(m_clock.restart()) / 1000.0);
}

void QcMusicPlayer::advance(double seconds)
{
    dropEndedPiece();
    const QcMusicDirector::Action action = director().advance(seconds);
    switch (action.kind) {
    case QcMusicDirector::Action::Kind::None:
        return;
    case QcMusicDirector::Action::Kind::PlayTrack:
        playTrack(action.track);
        return;
    case QcMusicDirector::Action::Kind::PlayGenerated:
        playGenerated();
        return;
    case QcMusicDirector::Action::Kind::FadeOut:
        fadeOut();
        return;
    }
}

void QcMusicPlayer::release()
{
    const QcAudioMixer::VoiceId playing = voice();
    m_output.withMixer([playing](QcAudioMixer &mixer) { mixer.stop(playing); });
    setVoice(0);
    setStream(nullptr);
    setPiece(nullptr);
}

void QcMusicPlayer::fadeOut()
{
    const QcAudioMixer::VoiceId playing = voice();
    const auto milliseconds = static_cast<int>(std::lround(QcMusicDirector::fadeSeconds * 1000.0));
    m_output.withMixer([playing, milliseconds](QcAudioMixer &mixer) { mixer.fadeOut(playing, milliseconds); });
}

void QcMusicPlayer::playTrack(const QString &id)
{
    const QcMusicTrack *track = director().library() ? director().library()->track(id) : nullptr;
    if (!track || !m_output.isActive()) {
        director().failed();
        return;
    }

    m_trackWatcher.setFuture(QtConcurrent::run([path = track->path]() -> QcResult<OpenedTrack> {
        QcResult<std::unique_ptr<QcVorbisStream>> stream = QcVorbisStream::open(path);
        if (!stream) {
            return qcError(stream.error());
        }
        const double seconds = (*stream)->seconds();
        return OpenedTrack{std::make_shared<QcStreamCursor>(std::shared_ptr<QcAudioStream>(std::move(*stream))), seconds};
    }));
}

void QcMusicPlayer::trackOpened()
{
    QcResult<OpenedTrack> opened = m_trackWatcher.result();
    if (opened && opened->seconds <= 0.0) {
        // Without its last granule position the stream's length is unknown.
        opened = qcError(u"the file gives no length"_s);
    }
    if (!opened) {
        qCWarning(lcAudio) << "Music track not played:" << opened.error();
        director().failed();
        return;
    }
    if (director().state() != QcMusicDirector::State::Asking || !m_output.isActive()) {
        // Switched off or interrupted while it opened.
        return;
    }

    release();
    setStream(std::move(opened->cursor));
    QcAudioMixer::VoiceId played = 0;
    m_output.withMixer([this, &played](QcAudioMixer &mixer) { played = mixer.playStream(stream(), {musicGain, 0.0f, 1.0, QcSoundCategory::Music, false}); });
    begin(played, opened->seconds);
}

void QcMusicPlayer::begin(QcAudioMixer::VoiceId played, double seconds)
{
    setVoice(played);
    if (played == 0) {
        director().failed();
        release();
        return;
    }
    if (!director().started(seconds)) {
        release();
    }
}

void QcMusicPlayer::playGenerated()
{
    if (!m_output.isActive()) {
        director().failed();
        return;
    }

    const quint64 seed = QRandomGenerator::global()->generate64();
    const int rate = m_output.sampleRate();
    // The place's layout and, where the biome brings no music of its own, its style (E-1156).
    const QcMusicArrangement arrangement = nextArrangement();
    const QcMusicStyle style = arrangement.styleFor(m_style);
    m_stopRendering = std::make_shared<std::atomic<bool>>(false);
    m_pieceWatcher.setFuture(QtConcurrent::run([seed, rate, style, arrangement, stop = m_stopRendering] {
        const QcMusicComposer::Piece piece = QcMusicComposer::compose(seed, style, arrangement);
        return std::make_shared<const QcSampleBuffer>(QcMusicComposer::render(piece, rate, stop.get()));
    }));
}

void QcMusicPlayer::pieceRendered()
{
    // Take the piece out of the future and let the future go, so that only this player (and the
    // mixer while it plays) holds the piece, not the watcher through the pause after it. The
    // empty future set here reports itself finished too: nothing to take then.
    using RenderedPiece = std::shared_ptr<const QcSampleBuffer>;
    if (m_pieceWatcher.future().resultCount() == 0) {
        return;
    }
    RenderedPiece rendered = m_pieceWatcher.result();
    m_pieceWatcher.setFuture(QFuture<RenderedPiece>());
    if (director().state() != QcMusicDirector::State::Asking || !m_output.isActive()) {
        return;
    }
    if (!rendered || rendered->samples.empty()) {
        director().failed();
        return;
    }

    release();
    setPiece(std::move(rendered));
    QcAudioMixer::VoiceId played = 0;
    m_output.withMixer([this, &played](QcAudioMixer &mixer) { played = mixer.play(piece(), {musicGain, 0.0f, 1.0, QcSoundCategory::Music, false}); });
    begin(played, piece()->seconds());
}
