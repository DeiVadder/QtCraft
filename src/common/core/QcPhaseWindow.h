// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QtGlobal>

#include <span>
#include <vector>

/*! \brief Mean and maximum of a few series of times (milliseconds) over the latest samples, for the
    debug display (decision E-225): one sample per server tick or per frame, each with one value per
    series.

    The memory is taken once in the constructor; adding a sample neither allocates nor scans, the
    mean keeps running sums and the maximum is looked up when asked for. Not thread-safe. */
class QcPhaseWindow
{
public:
    QcPhaseWindow(int series, int capacity);

    /*! Returns the number of series. */
    int seriesCount() const { return m_series; }
    /*! Returns the most samples held. */
    int capacity() const { return m_capacity; }
    /*! Samples held, at most the capacity. */
    int size() const { return m_size; }

    /*! One sample; \a values must hold one value per series (missing ones count as 0, extra ones
        are ignored). The oldest sample leaves once the window is full. */
    void add(std::span<const float> values);
    /*! Drops all samples. */
    void clear();

    /*! Returns the mean of \a series over the samples held; 0 without samples. */
    double mean(int series) const;
    /*! Returns the maximum of \a series over the samples held; 0 without samples. */
    double max(int series) const;
    /*! Returns the newest value of \a series; 0 without samples. */
    float latest(int series) const;
    /*! Returns up to \a count values of \a series, oldest first (graphs). */
    QList<float> history(int series, int count) const;

private:
    float value(int sample, int series) const
    {
        return m_values[static_cast<std::size_t>(sample) * static_cast<std::size_t>(m_series) + static_cast<std::size_t>(series)];
    }

    int m_series = 0;
    int m_capacity = 0;
    int m_size = 0;
    // Index of the slot the next sample goes into.
    int m_next = 0;
    std::vector<float> m_values;
    std::vector<double> m_sums;
};
