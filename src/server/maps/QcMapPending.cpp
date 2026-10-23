#include "server/maps/QcMapPending.h"

#include <algorithm>

QcMapPending QcMapPending::whole()
{
    QcMapPending pending;
    pending.m_rows.fill(Span{0, QcMapData::size - 1});
    return pending;
}

void QcMapPending::add(QPoint pixel)
{
    if (!QcMapData::bounds().contains(pixel)) {
        return;
    }
    Span &row = m_rows[static_cast<std::size_t>(pixel.y())];
    row.left = std::min(row.left, pixel.x());
    row.right = std::max(row.right, pixel.x());
}

QRect QcMapPending::take(int budget)
{
    for (int step = 0; step < QcMapData::size; ++step) {
        const int first = (m_cursor + step) % QcMapData::size;
        Span span = m_rows[static_cast<std::size_t>(first)];
        if (span.isEmpty()) {
            continue;
        }
        if (span.right - span.left + 1 > budget) {
            return {};
        }

        // Adjacent changed rows join while the rectangle stays within the budget.
        int last = first;
        while (last + 1 < QcMapData::size && !m_rows[static_cast<std::size_t>(last) + 1].isEmpty()) {
            const Span next = m_rows[static_cast<std::size_t>(last) + 1];
            const Span joined{std::min(span.left, next.left), std::max(span.right, next.right)};
            if ((joined.right - joined.left + 1) * (last + 2 - first) > budget) {
                break;
            }
            span = joined;
            ++last;
        }

        std::fill(m_rows.begin() + first, m_rows.begin() + last + 1, Span{});
        m_cursor = (last + 1) % QcMapData::size;
        return QRect(QPoint(span.left, first), QPoint(span.right, last));
    }
    return {};
}

bool QcMapPending::isEmpty() const
{
    return std::ranges::all_of(m_rows, [](Span row) { return row.isEmpty(); });
}
