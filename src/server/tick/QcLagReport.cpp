#include "server/tick/QcLagReport.h"

#include <QStringList>

#include <algorithm>
#include <numeric>
#include <ranges>

using namespace Qt::StringLiterals;

namespace {

QString milliseconds(qint64 ns)
{
    return QString::number(static_cast<double>(ns) / 1e6, 'f', 1);
}

} // namespace

std::optional<QString> QcLagReport::check(const QcTickPhaseTimer::Durations &phases, qint64 nowNs)
{
    const qint64 tickNs = std::accumulate(phases.begin(), phases.begin() + QcTickPhases::inTick, qint64{0});
    if (tickNs <= thresholdNs) {
        return std::nullopt;
    }
    if (m_lastLineNs && nowNs - *m_lastLineNs < quietNs) {
        ++m_suppressed;
        m_suppressedWorstNs = std::max(m_suppressedWorstNs, tickNs);
        return std::nullopt;
    }

    QString text = line(phases, tickNs);
    if (m_suppressed > 0) {
        text += u"; %1 more since the previous line, the longest %2 ms"_s.arg(m_suppressed).arg(milliseconds(m_suppressedWorstNs));
    }
    m_lastLineNs = nowNs;
    m_suppressed = 0;
    m_suppressedWorstNs = 0;
    return text;
}

std::optional<QString> QcLagReport::flush()
{
    if (m_suppressed == 0) {
        return std::nullopt;
    }
    QString text = u"Lag spikes: %1 more since the previous line, the longest %2 ms"_s.arg(m_suppressed).arg(milliseconds(m_suppressedWorstNs));
    m_suppressed = 0;
    m_suppressedWorstNs = 0;
    return text;
}

QString QcLagReport::line(const QcTickPhaseTimer::Durations &phases, qint64 tickNs) const
{
    std::array<int, QcTickPhases::inTick> order{};
    std::ranges::iota(order, 0);
    std::ranges::stable_sort(order, std::greater{}, [&phases](int phase) { return phases[static_cast<std::size_t>(phase)]; });

    QStringList parts;
    for (const int phase : order | std::views::take(shownPhases)) {
        if (phases[static_cast<std::size_t>(phase)] > 0) {
            parts.append(u"%1 %2"_s.arg(QLatin1StringView(QcTickPhases::ids[static_cast<std::size_t>(phase)]),
                milliseconds(phases[static_cast<std::size_t>(phase)])));
        }
    }
    return u"Lag spike: tick took %1 ms (%2)"_s.arg(milliseconds(tickNs), parts.join(u", "_s));
}
