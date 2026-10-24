#include "common/physics/QcBodyGrid.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

qint64 QcBodyGrid::columnOf(int x, int z)
{
    return (static_cast<qint64>(x) << 32) | static_cast<qint64>(static_cast<quint32>(z));
}

void QcBodyGrid::clear()
{
    m_entries.clear();
    m_widest = 0.0;
}

void QcBodyGrid::add(const QcBodyPush::Body &body)
{
    const int x = static_cast<int>(std::floor(body.feet.x));
    const int z = static_cast<int>(std::floor(body.feet.z));
    m_entries.push_back({columnOf(x, z), static_cast<qsizetype>(m_entries.size()), body});
    m_widest = std::max(m_widest, body.halfWidth);
}

void QcBodyGrid::finish()
{
    // By column and then by the order they came in, so a sum adds the bodies of a column alike every
    // time; unlike a stable sort this needs no buffer.
    std::ranges::sort(m_entries, std::less<>(), [](const Entry &entry) { return std::pair{entry.column, entry.order}; });
}

QcVec3 QcBodyGrid::sum(const QcBodyPush::Body &self) const
{
    QcVec3 total;
    if (m_entries.empty()) {
        return total;
    }

    // Every column a body within reach can stand in: its feet lie at most the two half widths away.
    const double reach = self.halfWidth + m_widest;
    const int fromX = static_cast<int>(std::floor(self.feet.x - reach));
    const int toX = static_cast<int>(std::floor(self.feet.x + reach));
    const int fromZ = static_cast<int>(std::floor(self.feet.z - reach));
    const int toZ = static_cast<int>(std::floor(self.feet.z + reach));
    for (int x = fromX; x <= toX; ++x) {
        for (int z = fromZ; z <= toZ; ++z) {
            const auto [first, last] = std::ranges::equal_range(m_entries, columnOf(x, z), std::less<>(), &Entry::column);
            for (auto it = first; it != last; ++it) {
                total += QcBodyPush::between(self, it->body);
            }
        }
    }
    return total;
}
