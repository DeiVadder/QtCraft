#pragma once

#include <QList>
#include <QtGlobal>

#include <span>
#include <vector>

// Mean and maximum of a few series of times (milliseconds) over the latest samples, for the debug
// display (decision E-225): one sample per server tick or per frame, each with one value per series.
// The memory is taken once in the constructor; adding a sample neither allocates nor scans, the
// mean keeps running sums and the maximum is looked up when asked for. Not thread-safe.
class QcPhaseWindow
{
public:
    QcPhaseWindow(int series, int capacity);

    int seriesCount() const { return m_series; }
    int capacity() const { return m_capacity; }
    // Samples held, at most the capacity.
    int size() const { return m_size; }

    // One sample; `values` must hold one value per series (missing ones count as 0, extra ones are
    // ignored). The oldest sample leaves once the window is full.
    void add(std::span<const float> values);
    void clear();

    double mean(int series) const;
    double max(int series) const;
    // The newest value of a series; 0 without samples.
    float latest(int series) const;
    // Up to `count` values of a series, oldest first (graphs).
    QList<float> history(int series, int count) const;

private:
    float value(int sample, int series) const { return m_values[std::size_t(sample) * std::size_t(m_series) + std::size_t(series)]; }

    int m_series = 0;
    int m_capacity = 0;
    int m_size = 0;
    // Index of the slot the next sample goes into.
    int m_next = 0;
    std::vector<float> m_values;
    std::vector<double> m_sums;
};
