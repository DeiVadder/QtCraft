// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

#include <optional>

/*! \brief Mean and maximum of durations within one interval of the health line (decision E-834). */
struct QcIntervalTiming
{
    /*! The mean duration. */
    double meanMs = 0.0;
    /*! The longest duration. */
    double maxMs = 0.0;
    /*! The number of durations. */
    quint64 count = 0;
};

/*! \brief Collects durations until take() hands out their mean and maximum and starts anew.

    One thread. */
class QcIntervalStats
{
public:
    /*! Adds a duration of \a ms milliseconds. */
    void add(double ms);
    /*! Returns the interval so far; std::nullopt when nothing was added. */
    std::optional<QcIntervalTiming> take();

private:
    double m_totalMs = 0.0;
    double m_maxMs = 0.0;
    quint64 m_count = 0;
};
