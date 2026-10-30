// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

#include <deque>
#include <optional>

/*! \brief The round trip of a connection as the debug display, the control interface and the
    health line show it (decision E-886), in milliseconds: the latest sample, the smoothed value
    (EWMA with 1/8 like TCP's SRTT), the smallest and largest sample and the jitter (mean
    difference between consecutive samples) over the window. */
struct QcRoundTripSummary
{
    /*! The latest sample. */
    double lastMs = 0.0;
    /*! The smoothed value. */
    double smoothedMs = 0.0;
    /*! The smallest and largest sample in the window. \qcshared */
    double minMs = 0.0;
    double maxMs = 0.0;
    /*! The mean difference between consecutive samples in the window. */
    double jitterMs = 0.0;
    /*! Samples in the window. */
    int samples = 0;
    /*! Age of a probe still waiting for its echo when it is older than the smoothed value, a
        lower bound of the current round trip that makes a stalled connection visible; 0
        otherwise. */
    double pendingMs = 0.0;

    /*! Returns \a ms as the displays show them: one decimal below 10 ms, whole ones above. */
    static QString text(double ms);
};

/*! \brief The round trip as the player list carries it (decision E-886), in microseconds; negative
    while unknown.

    The client reports it at display resolution, the server lists a new value only when it shows:
    so the list does not go out again for noise. */
struct QcListedLatency
{
    /*! \a ms rounded to what the displays show (QcRoundTripSummary::text): 0.1 ms below 10 ms,
        whole milliseconds above, at most QcMsg::LatencyProbe::maxReportedUs. */
    static qint32 quantizedUs(double ms);
    /*! Returns the signal bars of \a latencyUs as in the reference: 5 below 150 ms, 4 below 300,
        3 below 600, 2 below 1000, else 1; 0 while unknown. */
    static int bars(qint32 latencyUs);
    /*! Returns whether \a reportedUs differs visibly from the listed \a listedUs: by at least 1
        ms and 10 % of the listed value, by a signal bar, or between unknown and known. */
    static bool changed(qint32 listedUs, qint32 reportedUs);
};

/*! \brief Round trip samples of the last \c {windowNs} (decision E-886).

    Plain data for one thread; whoever shares it guards it (QcLatencyProbe). */
class QcRoundTripStats
{
public:
    /*! The window's default length: ten seconds. */
    static constexpr qint64 defaultWindowNs = 10'000'000'000;
    /*! Weight of a new sample in the smoothed value. */
    static constexpr double smoothing = 1.0 / 8.0;

    explicit QcRoundTripStats(qint64 windowNs = defaultWindowNs)
        : m_windowNs(windowNs)
    {
    }

    /*! A round trip of \a roundTripNs that ended at \a atNs (a steady clock). */
    void add(qint64 atNs, qint64 roundTripNs);
    /*! Returns the window as it is at \a nowNs; std::nullopt before the first sample. The
        smoothed value and the latest sample stay when the window has run empty. */
    [[nodiscard]] std::optional<QcRoundTripSummary> summary(qint64 nowNs) const;

private:
    struct Sample
    {
        qint64 atNs = 0;
        qint64 roundTripNs = 0;
    };

    qint64 m_windowNs = defaultWindowNs;
    std::deque<Sample> m_samples;
    std::optional<double> m_smoothedMs;
    double m_lastMs = 0.0;
};
