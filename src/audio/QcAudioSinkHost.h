// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "audio/QcMixerDevice.h"

#include <QAudio>
#include <QObject>

#include <memory>

class QAudioDevice;
class QAudioFormat;
class QAudioSink;

/*! \brief The audio sink and the device it pulls from, living on the audio thread of QcSystemAudio:
    Qt's sinks pull (and refill their buffers) on the thread that owns them, so a stalled GUI thread
    (a screen opening for the first time) would otherwise let the sound drop out.

    QcSystemAudio calls open() and close() on that thread and waits for them. */
class QcAudioSinkHost : public QObject
{
    Q_OBJECT

public:
    /*! \brief The result of open(). */
    struct Opened
    {
        /*! QtAudio::NoError when the sink plays. */
        QtAudio::Error error = QtAudio::NoError;
        /*! Counts the sinks opened; failed() names the sink it is about. */
        quint64 generation = 0;
    };

    /*! A host without a sink; QcSystemAudio moves it to its audio thread. */
    QcAudioSinkHost();
    ~QcAudioSinkHost() override;
    Q_DISABLE_COPY_MOVE(QcAudioSinkHost)

    /*! Starts a sink on \a device in \a format that pulls \a render with samples of
        \a sampleFormat and keeps \a bufferMilliseconds of audio buffered; on an error nothing stays
        open. Call on the host's thread. */
    [[nodiscard]] Opened open(const QAudioDevice &device, const QAudioFormat &format, QcMixerDevice::Format sampleFormat,
                              const QcMixerDevice::Render &render, int bufferMilliseconds);
    /*! Stops the sink, if one plays. Call on the host's thread. */
    void close();

signals:
    /*! The sink of \a generation stopped with \a error while playing and is closed. */
    void failed(quint64 generation, int error);

private:
    void closeAfterError(quint64 generation);

    std::unique_ptr<QcMixerDevice> m_device;
    // The sink reads from the device: declared after it, so it stops first.
    std::unique_ptr<QAudioSink> m_sink;
    // Generation of the open sink (or of the last one).
    quint64 m_generation = 0;
};
