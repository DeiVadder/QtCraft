#include "audio/QcAudioOutput.h"

#include "audio/QcAudioLogging.h"
#include "audio/QcAudioRoute.h"
#include "audio/QcSystemAudio.h"

#include <utility>

using namespace Qt::StringLiterals;

namespace {

// The device with `id` among `devices`.
std::optional<QcAudioPlatform::Device> findDevice(const QList<QcAudioPlatform::Device> &devices, const QByteArray &id)
{
    for (const QcAudioPlatform::Device &device : devices) {
        if (device.id == id) {
            return device;
        }
    }
    return std::nullopt;
}

} // namespace

QcAudioOutput::QcAudioOutput(QObject *parent)
    : QcAudioOutput(std::make_unique<QcSystemAudio>(), parent)
{
}

QcAudioOutput::QcAudioOutput(std::unique_ptr<QcAudioPlatform> platform, QObject *parent)
    : QObject(parent)
    , m_platform(std::move(platform))
{
    m_retryTimer.setSingleShot(true);
    m_retryTimer.setInterval(retryDelay);
    connect(&m_retryTimer, &QTimer::timeout, this, &QcAudioOutput::retry);
    connect(m_platform.get(), &QcAudioPlatform::outputsChanged, this, &QcAudioOutput::onOutputsChanged);
    connect(m_platform.get(), &QcAudioPlatform::failed, this, &QcAudioOutput::onSinkFailed);
}

QcAudioOutput::~QcAudioOutput()
{
    stop();
}

void QcAudioOutput::fail(const QString &reason)
{
    m_error = reason;
    qCWarning(lcAudio).noquote() << "No sound:" << reason;
}

bool QcAudioOutput::start()
{
    m_platform->watchOutputs(true);
    if (isActive()) {
        return true;
    }

    m_retries = 0;
    const std::optional<QcAudioPlatform::Device> device = findDevice(m_platform->outputs(), m_platform->defaultOutput());
    if (!device) {
        fail(u"no audio output device"_s);
        return false;
    }
    if (!open(*device)) {
        scheduleRetry();
        return false;
    }
    return true;
}

std::optional<QcAudioMixer> QcAudioOutput::replaceMixer(int rate)
{
    const std::scoped_lock lock(m_mutex);
    if (rate == m_mixer.outputRate()) {
        return std::nullopt;
    }
    return std::exchange(m_mixer, m_mixer.withRate(rate));
}

void QcAudioOutput::restoreMixer(QcAudioMixer previous)
{
    const std::scoped_lock lock(m_mutex);
    m_mixer = std::move(previous);
}

bool QcAudioOutput::open(const QcAudioPlatform::Device &device)
{
    const std::optional<QcAudioPlatform::Format> format = m_platform->format(device.id, m_announcedRate);
    if (!format) {
        fail(u"the device '%1' supports neither float nor 16-bit stereo output"_s.arg(device.description));
        return false;
    }

    // The mixer runs at the device's rate: a new rate needs a new mixer, which takes over the
    // volumes and the mute switch. No sink reads it now (every open follows a closeSink). When the
    // device does not open, the old mixer comes back with its voices: the mixer always runs at
    // the announced rate.
    const int rate = format->rate;
    std::optional<QcAudioMixer> previous = replaceMixer(rate);
    const QcAudioPlatform::Opened opened = m_platform->open(device.id, *format, [this](int frames) { return render(frames); });
    if (opened.error != QtAudio::NoError) {
        if (previous) {
            restoreMixer(std::move(*previous));
        }
        fail(u"the audio output '%1' could not be started (error %2)"_s.arg(device.description).arg(static_cast<int>(opened.error)));
        return false;
    }

    m_generation = opened.generation;
    m_deviceId = device.id;
    m_openedAt.start();
    m_error.clear();
    qCInfo(lcAudio).noquote() << u"Audio output: %1, %2 Hz, %3"_s.arg(device.description)
                                     .arg(rate)
                                     .arg(format->sampleFormat == QcMixerDevice::Format::Float ? u"float"_s : u"16 bit"_s);
    if (rate != m_announcedRate) {
        m_announcedRate = rate;
        emit mixerReset();
    }
    return true;
}

void QcAudioOutput::closeSink()
{
    m_platform->close();
    m_deviceId.clear();
}

void QcAudioOutput::stop()
{
    m_retryTimer.stop();
    m_platform->watchOutputs(false);
    closeSink();
}

void QcAudioOutput::onOutputsChanged()
{
    // A device change is a new situation: the attempts after a sink error start again.
    m_retries = 0;
    m_retryTimer.stop();
    followDevices();
    if (!isActive() && !m_platform->outputs().isEmpty()) {
        scheduleRetry();
    }
}

void QcAudioOutput::followDevices()
{
    const QList<QcAudioPlatform::Device> outputs = m_platform->outputs();
    QList<QByteArray> ids;
    for (const QcAudioPlatform::Device &device : outputs) {
        ids.append(device.id);
    }

    const QcAudioRoute::Decision decision = QcAudioRoute::decide(m_deviceId, ids, m_platform->defaultOutput());
    if (decision.action == QcAudioRoute::Decision::Action::Keep) {
        return;
    }

    const QByteArray previous = m_deviceId.isEmpty() ? QByteArrayLiteral("none") : m_deviceId;
    closeSink();
    if (decision.action == QcAudioRoute::Decision::Action::Close) {
        qCInfo(lcAudio).noquote() << "Audio output" << previous << "is gone, no device left: silent";
        return;
    }

    qCInfo(lcAudio).noquote() << "Audio output changes:" << previous << "->" << decision.device << "(mixer at" << sampleRate()
                              << "Hz)";
    const std::optional<QcAudioPlatform::Device> device = findDevice(outputs, decision.device);
    if (!device || !open(*device)) {
        qCWarning(lcAudio).noquote() << "Audio output" << decision.device << "could not be opened";
    }
}

void QcAudioOutput::onSinkFailed(quint64 generation, int error)
{
    // A late report about a sink closed or replaced since.
    if (generation != m_generation || !isActive()) {
        return;
    }

    if (m_openedAt.elapsed() >= stableMilliseconds) {
        m_retries = 0;
    }
    m_deviceId.clear();
    fail(u"the audio output stopped (error %1)"_s.arg(error));
    scheduleRetry();
}

void QcAudioOutput::scheduleRetry()
{
    if (m_retries >= maxRetries) {
        qCWarning(lcAudio).noquote() << "Audio output: giving up after" << m_retries << "attempts, silent until the devices change";
        return;
    }

    ++m_retries;
    qCInfo(lcAudio).noquote() << "Audio output: attempt" << m_retries << "of" << maxRetries << "in" << m_retryTimer.interval() << "ms";
    m_retryTimer.start();
}

void QcAudioOutput::retry()
{
    if (isActive()) {
        return;
    }

    followDevices();
    if (!isActive()) {
        scheduleRetry();
    }
}

int QcAudioOutput::sampleRate() const
{
    const std::scoped_lock lock(m_mutex);
    return m_mixer.outputRate();
}

void QcAudioOutput::withMixer(const std::function<void(QcAudioMixer &)> &change)
{
    const std::scoped_lock lock(m_mutex);
    change(m_mixer);
}

std::span<const float> QcAudioOutput::render(int frames)
{
    const std::scoped_lock lock(m_mutex);
    m_framesRendered += frames;
    // The mixer's buffer changes only in mix(), which only the audio thread calls, so the view
    // stays valid after the lock is released.
    return m_mixer.mix(frames);
}
