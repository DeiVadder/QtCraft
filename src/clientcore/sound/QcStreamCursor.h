// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/sound/QcAudioStream.h"

#include <QtTypes>

#include <array>
#include <memory>
#include <optional>
#include <vector>

/*! \brief Reads a QcAudioStream for a mixer voice (decision E-1135): a small window of decoded
    stereo frames that slides along the stream as the voice's position advances, so that the voice
    can step through the stream at any speed and interpolate between frames.

    The window is allocated once, in the constructor; reading allocates nothing. Not thread-safe:
    the mixer reads it under the output's lock. */
class QcStreamCursor
{
public:
    /*! Frames decoded per refill of the window. */
    static constexpr int windowFrames = 1024;

    /*! A cursor at the start of \a stream (not null). */
    explicit QcStreamCursor(std::shared_ptr<QcAudioStream> stream);

    /*! Returns the frames per second of the stream. */
    [[nodiscard]] int sampleRate() const { return m_stream->sampleRate(); }
    /*! Returns the stereo frame (left, right) at \a position in frames from the start, linearly
        interpolated between its neighbours; none once \a position is past the last frame.
        Positions must not go back by more than a frame. */
    [[nodiscard]] std::optional<std::array<float, 2>> frameAt(double position);
    /*! Returns whether a read went past the last frame. */
    [[nodiscard]] bool ended() const { return m_ended; }

private:
    // Keeps the window's last frame (the left neighbour of the next one) and decodes the next
    // frames after it.
    void refill();
    // The window's last frame after it, as an index into the stream.
    [[nodiscard]] qint64 windowEnd() const { return m_start + m_count; }

    std::shared_ptr<QcAudioStream> m_stream;
    // Interleaved stereo frames; frame 0 is the stream's frame m_start, m_count are valid.
    std::vector<float> m_window; // NOLINT(qc-member-access): the window of refill() and frameAt(), no state of its own
    qint64 m_start = 0;          // NOLINT(qc-member-access): part of the window, see m_window
    int m_count = 0;             // NOLINT(qc-member-access): part of the window, see m_window
    // The stream gave no more frames.
    bool m_drained = false;
    bool m_ended = false;
};
