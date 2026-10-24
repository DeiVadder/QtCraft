#include "audio/QcSystemAudio.h"

#include "audio/QcAudioSinkHost.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QMediaDevices>

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

} // namespace

QcSystemAudio::QcSystemAudio(QObject *parent)
    : QcAudioPlatform(parent)
{
}

QcSystemAudio::~QcSystemAudio()
{
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
    Opened opened;
    QMetaObject::invokeMethod(
        m_host.get(),
        [&] {
            const QcAudioSinkHost::Opened result = m_host->open(output, audioFormat, format.sampleFormat, render, bufferMilliseconds);
            opened = {result.error, result.generation};
        },
        Qt::BlockingQueuedConnection);
    return opened;
}

void QcSystemAudio::close()
{
    if (m_host && m_thread.isRunning()) {
        QMetaObject::invokeMethod(m_host.get(), [this] { m_host->close(); }, Qt::BlockingQueuedConnection);
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
