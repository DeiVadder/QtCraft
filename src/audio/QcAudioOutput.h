#pragma once

#include "clientcore/sound/QcAudioMixer.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <span>

class QAudioSink;
class QcMixerDevice;

// Plays the mixer through the default audio output (QAudioSink in pull mode). The sink may read
// from another thread, so every access to the mixer goes through withMixer(). Without a usable
// device the output stays inactive and the game is silent.
class QcAudioOutput : public QObject
{
    Q_OBJECT

public:
    // Audio the sink keeps buffered: short enough for direct feedback, long enough to ride out a
    // slow frame.
    static constexpr int bufferMilliseconds = 80;

    explicit QcAudioOutput(QObject *parent = nullptr);
    ~QcAudioOutput() override;

    // Opens the default output device. Returns false and keeps silent when there is none or it
    // cannot be opened; the reason is in error() and logged.
    [[nodiscard]] bool start();
    void stop();
    bool isActive() const { return m_sink != nullptr; }
    QString error() const { return m_error; }
    int sampleRate() const;

    // Runs `change` with the mixer locked against the audio thread.
    void withMixer(const std::function<void(QcAudioMixer &)> &change);
    // Stereo frames rendered so far; readable from any thread.
    qint64 framesRendered() const { return m_framesRendered.load(); }

private:
    // The next `frames` stereo frames of the mixer, as the device reads them.
    [[nodiscard]] std::span<const float> render(int frames);
    void fail(const QString &reason);

    mutable std::mutex m_mutex;
    QcAudioMixer m_mixer;
    // The sink reads from the device: declared after it, so it stops first.
    std::unique_ptr<QcMixerDevice> m_device;
    std::unique_ptr<QAudioSink> m_sink;
    std::atomic<qint64> m_framesRendered{0};
    QString m_error;
};
