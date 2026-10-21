#pragma once

#include <QtGlobal>

#include <optional>

// Mean and maximum of durations within one interval of the health line (decision E-834).
struct QcIntervalTiming
{
    double meanMs = 0.0;
    double maxMs = 0.0;
    quint64 count = 0;
};

// Collects durations until take() hands out their mean and maximum and starts anew. One thread.
class QcIntervalStats
{
public:
    void add(double ms);
    // The interval so far; std::nullopt when nothing was added.
    std::optional<QcIntervalTiming> take();

private:
    double m_totalMs = 0.0;
    double m_maxMs = 0.0;
    quint64 m_count = 0;
};
