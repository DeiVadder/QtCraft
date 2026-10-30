// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "audio/QcMixerDevice.h"

#include <QAudio>
#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>

#include <optional>

/*! \brief What QcAudioOutput needs from the system's audio: the output devices, the formats they
    take and a sink that pulls the mixer.

    QcSystemAudio is the one of Qt Multimedia; tests use a fake one to play through device changes
    and sink errors without a real device. */
class QcAudioPlatform : public QObject
{
    Q_OBJECT

public:
    /*! \brief An output device. */
    struct Device
    {
        /*! The system's id of the device. */
        QByteArray id;
        /*! Its name for people. */
        QString description;
    };

    /*! \brief A format an output device takes. */
    struct Format
    {
        /*! Frames per second. */
        int rate = 0;
        /*! The type of a sample. */
        QcMixerDevice::Format sampleFormat = QcMixerDevice::Format::Float;
    };

    /*! \brief The result of open(). */
    struct Opened
    {
        /*! QtAudio::NoError when the sink plays. */
        QtAudio::Error error = QtAudio::NoError;
        /*! Counts the sinks opened; failed() names the sink it is about. */
        quint64 generation = 0;
    };

    using QObject::QObject;

    /*! Returns the system's output devices. */
    [[nodiscard]] virtual QList<Device> outputs() const = 0;
    /*! Returns the id of the system's default output; empty when it has none. */
    [[nodiscard]] virtual QByteArray defaultOutput() const = 0;
    /*! Returns the format of \a device: float or 16-bit stereo at \a keepRate if \a device takes
        it (0: no rate to keep), else at the device's own rate; nullopt when the device takes
        neither or is not there. */
    [[nodiscard]] virtual std::optional<Format> format(const QByteArray &device, int keepRate) const = 0;
    /*! Starts a sink on \a device in \a format that pulls \a render, closing the one before; on an
        error nothing stays open. Returns when the sink plays (or failed to start). */
    [[nodiscard]] virtual Opened open(const QByteArray &device, const Format &format, const QcMixerDevice::Render &render) = 0;
    /*! Stops the sink; \c {render} is not called any more once this returns. */
    virtual void close() = 0;
    /*! While \a watch is true, outputsChanged() reports changes of the system's devices. */
    virtual void watchOutputs(bool watch) = 0;

signals:
    /*! The system's devices or its default output changed (while watchOutputs() is on). */
    void outputsChanged();
    /*! The sink of \a generation stopped with \a error while playing and is closed. */
    void failed(quint64 generation, int error);
};
