#include "common/protocol/QcAdminSamples.h"

#include <algorithm>
#include <limits>

namespace QcAdminSamples {

QcMsg::AdminSample folded(const QList<QcMsg::AdminSample> &minute)
{
    QcMsg::AdminSample result = minute.last();
    const auto mean = [&minute](float QcMsg::AdminSample::*field) {
        double sum = 0.0;
        for (const QcMsg::AdminSample &sample : minute) {
            sum += static_cast<double>(sample.*field);
        }
        return static_cast<float>(sum / static_cast<double>(minute.size()));
    };
    const auto highest = [&minute](float QcMsg::AdminSample::*field) {
        return std::ranges::max(minute, {}, field).*field;
    };
    result.tickMeanMs = mean(&QcMsg::AdminSample::tickMeanMs);
    result.ticksPerSecond = mean(&QcMsg::AdminSample::ticksPerSecond);
    result.sentKbPerSecond = mean(&QcMsg::AdminSample::sentKbPerSecond);
    result.receivedKbPerSecond = mean(&QcMsg::AdminSample::receivedKbPerSecond);
    result.tickP99Ms = highest(&QcMsg::AdminSample::tickP99Ms);
    result.tickMaxMs = highest(&QcMsg::AdminSample::tickMaxMs);
    return result;
}

QcMsg::AdminHistory withMinuteFolded(QcMsg::AdminHistory history)
{
    // The server folds every 60 seconds; the newest minute carries the time of its last second.
    const qint64 after = history.minutes.isEmpty() ? std::numeric_limits<qint64>::min() : history.minutes.last().timeMs;
    const auto first = std::ranges::find_if(history.seconds, [after](const QcMsg::AdminSample &sample) { return sample.timeMs > after; });
    const QList<QcMsg::AdminSample> pending(first, history.seconds.end());
    if (pending.size() < secondsPerMinute) {
        return history;
    }

    if (history.minutes.size() >= minutesKept) {
        history.minutes.removeFirst();
    }
    history.minutes.append(folded(pending.first(secondsPerMinute)));
    return history;
}

} // namespace QcAdminSamples
