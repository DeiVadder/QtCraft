// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/sound/QcStreamCursor.h"

#include <algorithm>
#include <cmath>
#include <span>

QcStreamCursor::QcStreamCursor(std::shared_ptr<QcAudioStream> stream)
    : m_stream(std::move(stream))
    , m_window(static_cast<std::size_t>(windowFrames + 1) * 2, 0.0f)
{
}

void QcStreamCursor::refill()
{
    const int keep = m_count > 0 ? 1 : 0;
    if (keep > 0) {
        const auto last = static_cast<std::size_t>(m_count - 1) * 2;
        m_window[0] = m_window[last];
        m_window[1] = m_window[last + 1];
        m_start += m_count - 1;
    }

    const std::span<float> free = std::span<float>(m_window).subspan(static_cast<std::size_t>(keep) * 2);
    const int read = m_stream->read(free);
    if (read <= 0) {
        m_drained = true;
    }
    m_count = keep + std::max(0, read);
}

std::optional<std::array<float, 2>> QcStreamCursor::frameAt(double position)
{
    const auto index = std::max(m_start, static_cast<qint64>(std::floor(std::max(0.0, position))));
    while (index + 1 >= windowEnd() && !m_drained) {
        refill();
    }
    if (index >= windowEnd()) {
        m_ended = true;
        return std::nullopt;
    }

    const auto at = static_cast<std::size_t>(index - m_start) * 2;
    // The last frame of the stream has no right neighbour: it is held.
    const std::size_t next = index + 1 < windowEnd() ? at + 2 : at;
    const auto fraction = static_cast<float>(std::clamp(position - static_cast<double>(index), 0.0, 1.0));
    return std::array<float, 2>{m_window[at] + (m_window[next] - m_window[at]) * fraction,
                                m_window[at + 1] + (m_window[next + 1] - m_window[at + 1]) * fraction};
}
