// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QIODevice>

#include <functional>
#include <span>

/*! \brief Read-only device an audio sink pulls from: every read renders fresh interleaved stereo
    audio through \c {render} (a view of \c {frames} frames, valid until the next call), converted
    to the sink's sample format.

    It never runs dry. */
class QcMixerDevice : public QIODevice
{
    Q_OBJECT

public:
    /*! The sample formats of a sink. */
    enum class Format {
        /*! 32-bit float. */
        Float,
        /*! 16-bit signed integer. */
        Int16
    };
    /*! Renders the next frames (interleaved stereo) for the device. */
    using Render = std::function<std::span<const float>(int frames)>;

    /*! A device that renders through \a render and converts to \a format, child of \a parent. */
    QcMixerDevice(Render render, Format format, QObject *parent = nullptr);

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override;

protected:
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData(const char *data, qint64 maxSize) override;

private:
    int bytesPerFrame() const;

    Render m_render;
    Format m_format;
};
