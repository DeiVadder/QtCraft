#include "common/core/QcPhaseWindow.h"

#include <algorithm>

QcPhaseWindow::QcPhaseWindow(int series, int capacity)
    : m_series(std::max(series, 1))
    , m_capacity(std::max(capacity, 1))
    , m_values(static_cast<std::size_t>(m_series) * static_cast<std::size_t>(m_capacity), 0.0f)
    , m_sums(static_cast<std::size_t>(m_series), 0.0)
{
}

void QcPhaseWindow::add(std::span<const float> values)
{
    float *slot = m_values.data() + static_cast<std::size_t>(m_next) * static_cast<std::size_t>(m_series);
    const bool full = m_size == m_capacity;

    for (int s = 0; s < m_series; ++s) {
        const float incoming = static_cast<std::size_t>(s) < values.size() ? values[static_cast<std::size_t>(s)] : 0.0f;
        if (full) {
            m_sums[static_cast<std::size_t>(s)] -= static_cast<double>(slot[s]);
        }
        m_sums[static_cast<std::size_t>(s)] += static_cast<double>(incoming);
        slot[s] = incoming;
    }

    m_next = (m_next + 1) % m_capacity;
    m_size = std::min(m_size + 1, m_capacity);
}

void QcPhaseWindow::clear()
{
    std::ranges::fill(m_values, 0.0f);
    std::ranges::fill(m_sums, 0.0);
    m_size = 0;
    m_next = 0;
}

double QcPhaseWindow::mean(int series) const
{
    if (m_size == 0 || series < 0 || series >= m_series) {
        return 0.0;
    }
    // Running sums of many additions and removals may end a hair below zero.
    return std::max(0.0, m_sums[static_cast<std::size_t>(series)] / static_cast<double>(m_size));
}

double QcPhaseWindow::max(int series) const
{
    if (series < 0 || series >= m_series) {
        return 0.0;
    }

    float highest = 0.0f;
    for (int sample = 0; sample < m_size; ++sample) {
        highest = std::max(highest, value(sample, series));
    }
    return static_cast<double>(highest);
}

float QcPhaseWindow::latest(int series) const
{
    if (m_size == 0 || series < 0 || series >= m_series) {
        return 0.0f;
    }
    return value((m_next + m_capacity - 1) % m_capacity, series);
}

QList<float> QcPhaseWindow::history(int series, int count) const
{
    QList<float> result;
    if (series < 0 || series >= m_series) {
        return result;
    }

    const int taken = std::clamp(count, 0, m_size);
    result.reserve(taken);
    // The oldest of the taken samples lies `taken` slots before the next free one.
    for (int i = taken; i > 0; --i) {
        result.append(value((m_next + m_capacity - i) % m_capacity, series));
    }
    return result;
}
