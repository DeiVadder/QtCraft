// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <span>

/*! \brief Audio decoded piece by piece while it plays (a music track, decision E-1135), instead of
    a whole QcSampleBuffer in memory.

    The mixer reads it on the audio thread (QcStreamCursor), under the lock of the output; read()
    must neither block on other threads nor allocate. Created and destroyed on other threads. */
class QcAudioStream
{
public:
    QcAudioStream() = default;
    virtual ~QcAudioStream() = default;
    QcAudioStream(const QcAudioStream &) = delete;
    QcAudioStream &operator=(const QcAudioStream &) = delete;
    QcAudioStream(QcAudioStream &&) = delete;
    QcAudioStream &operator=(QcAudioStream &&) = delete;

    /*! Returns the frames per second of the decoded audio. */
    [[nodiscard]] virtual int sampleRate() const = 0;
    /*! Returns the length in seconds (0 when unknown). */
    [[nodiscard]] virtual double seconds() const = 0;
    /*! Decodes the next frames into \a stereo (interleaved left, right; a mono stream gives both
        sides the same) and returns how many frames it wrote: fewer than fit only at the end, 0
        after it. */
    [[nodiscard]] virtual int read(std::span<float> stereo) = 0;
};
