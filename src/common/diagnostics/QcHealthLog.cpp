#include "common/diagnostics/QcHealthLog.h"

#include "common/core/QcLogging.h"
#include "common/core/QcProcessMemory.h"

#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <chrono>

using namespace Qt::StringLiterals;

namespace {

QString timing(const QcIntervalTiming &value)
{
    return u"%1 ms mean, %2 ms max"_s.arg(value.meanMs, 0, 'f', 1).arg(value.maxMs, 0, 'f', 1);
}

// KiB per second between two byte counts.
QString rate(quint64 now, quint64 before, double seconds)
{
    const double bytes = now >= before ? double(now - before) : 0.0;
    return QString::number(bytes / 1024.0 / std::max(seconds, 0.001), 'f', 1);
}

} // namespace

QcHealthLog::QcHealthLog(int intervalSeconds, Source source, QObject *parent)
    : QObject(parent)
    , m_source(std::move(source))
    , m_timer(new QTimer(this))
{
    // The first line's network rates count from now.
    m_previousTraffic = m_source().traffic;
    m_sinceLine.start();
    connect(m_timer, &QTimer::timeout, this, &QcHealthLog::writeLine);
    m_timer->start(std::chrono::seconds(intervalSeconds));
}

QString QcHealthLog::line(const QcHealthSample &sample, Memory memory, const std::optional<QcTraffic> &previous, double seconds)
{
    QStringList parts{u"memory %1 MiB (peak %2 MiB)"_s.arg(memory.residentMb, 0, 'f', 0).arg(memory.peakMb, 0, 'f', 0)};
    if (sample.frames) {
        const double fps = sample.frames->meanMs > 0.0 ? 1000.0 / sample.frames->meanMs : 0.0;
        parts.append(u"frames %1 fps, %2"_s.arg(fps, 0, 'f', 1).arg(timing(*sample.frames)));
    }
    if (sample.ticks) {
        parts.append(u"tick %1"_s.arg(timing(*sample.ticks)));
    }
    const auto count = [&parts](const QString &name, qint64 value) {
        if (value >= 0) {
            parts.append(u"%1 %2"_s.arg(name).arg(value));
        }
    };
    count(u"chunks"_s, sample.loadedChunks);
    count(u"entities"_s, sample.entities);
    count(u"players"_s, sample.players);
    if (sample.roundTrip) {
        parts.append(u"round trip %1"_s.arg(timing(*sample.roundTrip)));
    }
    if (sample.traffic) {
        const QcTraffic before = previous.value_or(QcTraffic{});
        parts.append(u"network out %1 KiB/s, in %2 KiB/s"_s.arg(rate(sample.traffic->sent, before.sent, seconds),
                                                               rate(sample.traffic->received, before.received, seconds)));
    }
    return u"HEALTH "_s + parts.join(u" | "_s);
}

void QcHealthLog::writeLine()
{
    const QcHealthSample sample = m_source();
    const double seconds = double(m_sinceLine.restart()) / 1000.0;
    qCInfo(lcHealth).noquote() << line(sample, {QcProcessMemory::currentMb(), QcProcessMemory::peakMb()}, m_previousTraffic, seconds);
    m_previousTraffic = sample.traffic;
}
