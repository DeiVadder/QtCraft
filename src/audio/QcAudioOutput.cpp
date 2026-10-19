#include "audio/QcAudioOutput.h"

#include "audio/QcAudioLogging.h"
#include "audio/QcMixerDevice.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QMediaDevices>

using namespace Qt::StringLiterals;

QcAudioOutput::QcAudioOutput(QObject *parent)
    : QObject(parent)
{
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
    if (m_sink) {
        return true;
    }

    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) {
        fail(u"no audio output device"_s);
        return false;
    }

    QAudioFormat format = device.preferredFormat();
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Float);
    QcMixerDevice::Format sampleFormat = QcMixerDevice::Format::Float;
    if (!device.isFormatSupported(format)) {
        format.setSampleFormat(QAudioFormat::Int16);
        sampleFormat = QcMixerDevice::Format::Int16;
    }
    if (!device.isFormatSupported(format) || format.sampleRate() <= 0) {
        fail(u"the device '%1' supports neither float nor 16-bit stereo output"_s.arg(device.description()));
        return false;
    }

    {
        const std::scoped_lock lock(m_mutex);
        m_mixer = QcAudioMixer(format.sampleRate());
    }

    m_device = std::make_unique<QcMixerDevice>([this](float *out, int frames) { render(out, frames); }, sampleFormat);
    m_device->open(QIODevice::ReadOnly);
    m_sink = std::make_unique<QAudioSink>(device, format);
    m_sink->setBufferSize(format.bytesForDuration(qint64(bufferMilliseconds) * 1000));
    m_sink->start(m_device.get());
    if (m_sink->error() != QtAudio::NoError) {
        const int code = int(m_sink->error());
        m_sink.reset();
        m_device.reset();
        fail(u"the audio output could not be started (error %1)"_s.arg(code));
        return false;
    }

    // A device that goes away later stops the sink; say so instead of going quiet without a word.
    connect(m_sink.get(), &QAudioSink::stateChanged, this, [this](QtAudio::State state) {
        if (state == QtAudio::StoppedState && m_sink && m_sink->error() != QtAudio::NoError) {
            qCWarning(lcAudio) << "Audio output stopped, error" << int(m_sink->error());
        }
    });

    m_error.clear();
    qCInfo(lcAudio).noquote() << u"Audio output: %1, %2 Hz, %3"_s.arg(device.description())
                                     .arg(format.sampleRate())
                                     .arg(sampleFormat == QcMixerDevice::Format::Float ? u"float"_s : u"16 bit"_s);
    return true;
}

void QcAudioOutput::stop()
{
    if (m_sink) {
        m_sink->stop();
        m_sink.reset();
    }
    m_device.reset();
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

void QcAudioOutput::render(float *out, int frames)
{
    const std::scoped_lock lock(m_mutex);
    m_mixer.mix(out, frames);
    m_framesRendered += frames;
}
