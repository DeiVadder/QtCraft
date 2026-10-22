#pragma once

#include "common/protocol/QcAdminMessages.h"

#include <QList>

#include <optional>

// The status history of the admin channel (decision E-848): the durations of the ticks since the
// latest sample, and the samples of the last secondsKept seconds (one a second) and of the last
// minutesKept minutes (one a minute, the seconds of the minute folded together: means of the means
// and rates, the highest p99 and maximum, the latest counts). Recorded whether or not an operator
// looks, so a chart opened later shows the time before it. Cheap: one entry per tick, one sample a
// second.
class QcAdminStatusRecorder
{
public:
    static constexpr qsizetype secondsKept = 600;
    static constexpr qsizetype minutesKept = 1440;
    // Tick durations kept between two samples; a sprint runs more, then mean and maximum still count
    // all of them and the p99 looks at the first ones.
    static constexpr qsizetype ticksKept = 8192;

    // What the server counts at the moment of a sample.
    struct Counts
    {
        qint64 timeMs = 0;
        quint32 players = 0;
        quint32 loadedChunks = 0;
        quint32 entities = 0;
        float memoryMb = 0.0f;
        // Bytes the network clients sent and received so far.
        quint64 sentBytes = 0;
        quint64 receivedBytes = 0;
    };

    void noteTick(double ms);
    // Ends the second that ran for `seconds` of real time: the sample of its ticks and `counts`,
    // kept in the history and returned.
    QcMsg::AdminSample sample(const Counts &counts, double seconds);

    const QList<QcMsg::AdminSample> &seconds() const { return m_seconds; }
    const QList<QcMsg::AdminSample> &minutes() const { return m_minutes; }
    // The latest sample; std::nullopt before the first second ended.
    std::optional<QcMsg::AdminSample> latest() const;

private:
    // Folds the samples of the minute that ended into one.
    void foldMinute();

    QList<double> m_ticks;
    qsizetype m_tickCount = 0;
    double m_tickSum = 0.0;
    double m_tickMax = 0.0;
    std::optional<Counts> m_previous;
    QList<QcMsg::AdminSample> m_seconds;
    QList<QcMsg::AdminSample> m_minutes;
    // Samples since the latest fold.
    qsizetype m_inMinute = 0;
};
