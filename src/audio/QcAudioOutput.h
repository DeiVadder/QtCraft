#pragma once

#include "audio/QcAudioPlatform.h"
#include "clientcore/sound/QcAudioMixer.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>

// Plays the mixer through the default audio output of a QcAudioPlatform (the system's,
// QcSystemAudio, unless a test passes another). The sink pulls the mixer on the audio thread, so
// every access to the mixer goes through withMixer(). Without a usable device the output stays
// inactive and the game is silent. From start() on it follows the system's devices (QcAudioRoute):
// a new default output, the current one going away or a first device appearing moves the sound to
// the default output, keeping the mixer with its voices when the new device takes the mixer's
// rate. A sink that fails while playing (a device invalidated by a format or profile change, which
// keeps the same default device) is opened again after retryDelay, at most maxRetries times in a
// row.
class QcAudioOutput : public QObject
{
    Q_OBJECT

public:
    // Attempts to open the output again after a sink error before giving up until the devices
    // change; a sink that played for stableMilliseconds before it failed starts a new series.
    static constexpr int maxRetries = 4;
    static constexpr std::chrono::milliseconds retryDelay{500};
    static constexpr qint64 stableMilliseconds = 5000;

    explicit QcAudioOutput(QObject *parent = nullptr);
    explicit QcAudioOutput(std::unique_ptr<QcAudioPlatform> platform, QObject *parent = nullptr);
    ~QcAudioOutput() override;

    // Opens the default output device and follows device changes from now on. Returns false and
    // keeps silent when there is none or it cannot be opened (until a usable device appears); the
    // reason is in error() and logged.
    [[nodiscard]] bool start();
    // Closes the device and stops following device changes.
    void stop();
    bool isActive() const { return !m_deviceId.isEmpty(); }
    QString error() const { return m_error; }
    int sampleRate() const;
    // Id of the device playing now; empty while none is open.
    QByteArray deviceId() const { return m_deviceId; }
    // The pause before opening the output again after a sink error (tests shorten it).
    void setRetryDelay(std::chrono::milliseconds delay) { m_retryTimer.setInterval(delay); }

    // Runs `change` with the mixer locked against the audio thread.
    void withMixer(const std::function<void(QcAudioMixer &)> &change);
    // Stereo frames rendered so far; readable from any thread.
    qint64 framesRendered() const { return m_framesRendered.load(); }

signals:
    // The mixer was replaced by a silent one at a new rate (the first device opened, or one that
    // does not take the old rate): sounds rendered for the old rate should be rendered again.
    void mixerReset();

private:
    // Opens `device`, at the mixer's rate if it takes it, else at its own (a new mixer then).
    [[nodiscard]] bool open(const QcAudioPlatform::Device &device);
    // A mixer at `rate` in place of the current one, which is returned; nullopt when the mixer
    // runs at `rate` already.
    [[nodiscard]] std::optional<QcAudioMixer> replaceMixer(int rate);
    void restoreMixer(QcAudioMixer previous);
    void closeSink();
    // Applies QcAudioRoute to the system's devices now.
    void followDevices();
    void onOutputsChanged();
    void onSinkFailed(quint64 generation, int error);
    // Opens the output again after a sink error, or gives up after maxRetries.
    void scheduleRetry();
    void retry();
    // The next `frames` stereo frames of the mixer, as the device reads them.
    [[nodiscard]] std::span<const float> render(int frames);
    void fail(const QString &reason);

    mutable std::mutex m_mutex;
    QcAudioMixer m_mixer;
    std::atomic<qint64> m_framesRendered{0};
    // Declared after the mixer: destroyed first, so no sink reads a destroyed mixer.
    std::unique_ptr<QcAudioPlatform> m_platform;
    // Sink generation of the open device (QcAudioPlatform::failed).
    quint64 m_generation = 0;
    QByteArray m_deviceId;
    // Rate of the mixer last announced with mixerReset (0: none yet).
    int m_announcedRate = 0;
    QString m_error;
    QTimer m_retryTimer;
    int m_retries = 0;
    // Since the open sink started.
    QElapsedTimer m_openedAt;
};
