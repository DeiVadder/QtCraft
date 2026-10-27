#include "audio/QcSystemAudio.h"

#include "audio/QcAudioSinkHost.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QMediaDevices>

#include <mutex>
#include <optional>
#include <type_traits>

using namespace Qt::StringLiterals;

namespace {

// The output device with `id`; a null device when it is not there.
QAudioDevice findOutput(const QByteArray &id)
{
    const QList<QAudioDevice> outputs = QMediaDevices::audioOutputs();
    for (const QAudioDevice &device : outputs) {
        if (device.id() == id) {
            return device;
        }
    }
    return {};
}

// Stereo at `rate` (the device's preferred rate when 0) in the sample format of `format`.
QAudioFormat stereo(const QAudioDevice &device, int rate, QcMixerDevice::Format sampleFormat)
{
    QAudioFormat format = device.preferredFormat();
    format.setChannelCount(2);
    if (rate > 0) {
        format.setSampleRate(rate);
    }
    format.setSampleFormat(sampleFormat == QcMixerDevice::Format::Float ? QAudioFormat::Float : QAudioFormat::Int16);
    return format;
}

// Float or 16-bit stereo at `rate` (the device's preferred rate when 0), if the device takes it.
std::optional<QcAudioPlatform::Format> formatAt(const QAudioDevice &device, int rate)
{
    for (const QcMixerDevice::Format sampleFormat : {QcMixerDevice::Format::Float, QcMixerDevice::Format::Int16}) {
        const QAudioFormat format = stereo(device, rate, sampleFormat);
        if (format.sampleRate() > 0 && device.isFormatSupported(format)) {
            return QcAudioPlatform::Format{format.sampleRate(), sampleFormat};
        }
    }
    return std::nullopt;
}

// Runs `work` on the thread of `host` and waits for it; empty when the call did not run. The
// blocking queued call carries only the wake-up and the address of the call: `work`, its result and
// everything it changed cross the threads through a std::mutex, which the thread sanitizer sees (it
// does not see the semaphore inside Qt). `work` gets the host as an argument, so nothing on the audio
// thread reads a member of QcSystemAudio.
template<typename Work>
auto callOnHostThread(QcAudioSinkHost *host, Work work)
{
    using Result = std::invoke_result_t<Work &, QcAudioSinkHost &>;
    using Stored = std::conditional_t<std::is_void_v<Result>, bool, Result>;
    struct Call
    {
        std::mutex mutex;
        QcAudioSinkHost *host = nullptr;
        Work work;
        std::optional<Stored> result;
    };

    Call call{{}, host, std::move(work), {}};
    {
        // Publishes the call (and what the caller did before) to the audio thread.
        const std::scoped_lock publish(call.mutex);
    }
    QMetaObject::invokeMethod(
        host,
        [&call] {
            const std::scoped_lock run(call.mutex);
            if constexpr (std::is_void_v<Result>) {
                call.work(*call.host);
                call.result = true;
            } else {
                call.result = call.work(*call.host);
            }
        },
        Qt::BlockingQueuedConnection);

    const std::scoped_lock collect(call.mutex);
    return call.result;
}

} // namespace

QcSystemAudio::QcSystemAudio(QObject *parent)
    : QcAudioPlatform(parent)
{
}

QcSystemAudio::~QcSystemAudio()
{
    // The host closes its sink on the audio thread, then the thread stops (nothing posts to the host
    // any more: the sink that could is gone) and only then is the host deleted here.
    close();
    m_thread.quit();
    m_thread.wait();
    m_host.reset();
}

QList<QcAudioPlatform::Device> QcSystemAudio::outputs() const
{
    QList<Device> devices;
    const QList<QAudioDevice> outputs = QMediaDevices::audioOutputs();
    for (const QAudioDevice &device : outputs) {
        devices.append({device.id(), device.description()});
    }
    return devices;
}

QByteArray QcSystemAudio::defaultOutput() const
{
    return QMediaDevices::defaultAudioOutput().id();
}

std::optional<QcAudioPlatform::Format> QcSystemAudio::format(const QByteArray &device, int keepRate) const
{
    const QAudioDevice output = findOutput(device);
    if (output.isNull()) {
        return std::nullopt;
    }

    if (keepRate > 0) {
        if (std::optional<Format> kept = formatAt(output, keepRate)) {
            return kept;
        }
    }
    return formatAt(output, 0);
}

void QcSystemAudio::startThread()
{
    if (m_host) {
        return;
    }

    m_host = std::make_unique<QcAudioSinkHost>();
    m_host->moveToThread(&m_thread);
    // Queued into the GUI thread, where QcAudioOutput lives.
    connect(m_host.get(), &QcAudioSinkHost::failed, this, &QcAudioPlatform::failed);
    m_thread.setObjectName(u"audio"_s);
    m_thread.start(QThread::TimeCriticalPriority);
}

QcAudioPlatform::Opened QcSystemAudio::open(const QByteArray &device, const Format &format, const QcMixerDevice::Render &render)
{
    const QAudioDevice output = findOutput(device);
    if (output.isNull()) {
        return {QtAudio::OpenError, 0};
    }

    startThread();
    const QAudioFormat audioFormat = stereo(output, format.rate, format.sampleFormat);
    const std::optional<QcAudioSinkHost::Opened> opened =
        callOnHostThread(m_host.get(), [output, audioFormat, sampleFormat = format.sampleFormat, render](QcAudioSinkHost &host) {
            return host.open(output, audioFormat, sampleFormat, render, bufferMilliseconds);
        });
    if (!opened) {
        return {QtAudio::OpenError, 0};
    }
    return {opened->error, opened->generation};
}

void QcSystemAudio::close()
{
    if (m_host && m_thread.isRunning()) {
        // An empty result (the call did not run) needs nothing: then there is no sink either.
        callOnHostThread(m_host.get(), [](QcAudioSinkHost &host) { host.close(); });
    }
}

void QcSystemAudio::watchOutputs(bool watch)
{
    if (!watch) {
        m_devices.reset();
        return;
    }
    if (!m_devices) {
        m_devices = std::make_unique<QMediaDevices>();
        connect(m_devices.get(), &QMediaDevices::audioOutputsChanged, this, &QcAudioPlatform::outputsChanged);
    }
}
