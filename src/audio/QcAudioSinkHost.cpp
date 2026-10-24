#include "audio/QcAudioSinkHost.h"

#include "audio/QcAudioLogging.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>

QcAudioSinkHost::QcAudioSinkHost() = default;

QcAudioSinkHost::~QcAudioSinkHost()
{
    close();
}

QcAudioSinkHost::Opened QcAudioSinkHost::open(const QAudioDevice &device, const QAudioFormat &format, QcMixerDevice::Format sampleFormat,
                                              const QcMixerDevice::Render &render, int bufferMilliseconds)
{
    close();
    ++m_generation;
    m_device = std::make_unique<QcMixerDevice>(render, sampleFormat);
    m_device->open(QIODevice::ReadOnly);
    m_sink = std::make_unique<QAudioSink>(device, format);
    m_sink->setBufferSize(format.bytesForDuration(static_cast<qint64>(bufferMilliseconds) * 1000));
    m_sink->start(m_device.get());
    const QtAudio::Error error = m_sink->error();
    if (error != QtAudio::NoError) {
        close();
        return {error, m_generation};
    }

    // A device that fails while playing stops the sink; the sink must not be deleted inside its
    // own signal, hence the queued close. The generation tells this sink from a later one.
    connect(m_sink.get(), &QAudioSink::stateChanged, this, [this, generation = m_generation](QtAudio::State state) {
        if (state == QtAudio::StoppedState && generation == m_generation && m_sink && m_sink->error() != QtAudio::NoError) {
            QMetaObject::invokeMethod(this, [this, generation] { closeAfterError(generation); }, Qt::QueuedConnection);
        }
    });
    return {QtAudio::NoError, m_generation};
}

void QcAudioSinkHost::closeAfterError(quint64 generation)
{
    // The sink may have been closed, or a new one opened, meanwhile.
    if (generation != m_generation || !m_sink) {
        return;
    }

    const auto error = static_cast<int>(m_sink->error());
    qCWarning(lcAudio) << "Audio output stopped, error" << error;
    close();
    emit failed(generation, error);
}

void QcAudioSinkHost::close()
{
    if (m_sink) {
        // A closed sink reports nothing any more (stop() itself changes the state).
        m_sink->disconnect(this);
        m_sink->stop();
        m_sink.reset();
    }
    m_device.reset();
}
