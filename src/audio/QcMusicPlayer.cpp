// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/QcMusicPlayer.h"

#include "audio/QcAudioLogging.h"
#include "audio/QcAudioOutput.h"
#include "clientcore/music/QcVorbisStream.h"
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
    // the watchers.
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
    release();
}

void QcMusicPlayer::interrupted()
{
    // The voice is gone with the mixer's: only the stream is left to drop.
    setVoice(0);
    setStream(nullptr);
    director().interrupted();
}

void QcMusicPlayer::tick()
{
    advance(static_cast<double>(m_clock.restart()) / 1000.0);
}

void QcMusicPlayer::advance(double seconds)
{
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
    m_output.withMixer([this, &played](QcAudioMixer &mixer) { played = mixer.playStream(stream(), {musicGain, 0.0f, 1.0, QcSoundBus::Music, false}); });
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
    const QcMusicStyle style = m_style;
    m_pieceWatcher.setFuture(QtConcurrent::run([seed, rate, style] {
        return std::make_shared<const QcSampleBuffer>(QcMusicComposer::render(QcMusicComposer::compose(seed, style), rate));
    }));
}

void QcMusicPlayer::pieceRendered()
{
    const std::shared_ptr<const QcSampleBuffer> piece = m_pieceWatcher.result();
    if (director().state() != QcMusicDirector::State::Asking || !m_output.isActive()) {
        return;
    }
    if (!piece || piece->samples.empty()) {
        director().failed();
        return;
    }

    release();
    QcAudioMixer::VoiceId played = 0;
    m_output.withMixer([&piece, &played](QcAudioMixer &mixer) { played = mixer.play(piece, {musicGain, 0.0f, 1.0, QcSoundBus::Music, false}); });
    begin(played, piece->seconds());
}
