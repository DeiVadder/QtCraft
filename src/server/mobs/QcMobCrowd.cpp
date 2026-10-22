#include "server/mobs/QcMobCrowd.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

int cellOf(double coordinate)
{
    return int(std::floor(coordinate));
}

// Direction to part a creature standing exactly on the centre of a crowd, fixed by its id and the cell.
QcVec3 partingDirection(quint32 self, QcBlockPos cell)
{
    const quint32 hash = (self * 2654435761u) ^ quint32(qHash(cell));
    const double angle = double(hash) * (2.0 * std::numbers::pi / 4294967296.0);
    return {std::cos(angle), 0.0, std::sin(angle)};
}

} // namespace

bool QcMobCrowd::takesPart(const QcCreature &mob, const QcMobDefinition &definition) const
{
    // Flyers and swimmers keep out of both crowds.
    if (m_kind == Members::Hoverers) {
        return definition.hover.has_value() && mob.chasing != 0;
    }
    return !definition.flying && !definition.hover && !definition.aquatic;
}

void QcMobCrowd::setThresholds(qint64 throttled, qint64 idle)
{
    m_throttled = int(throttled);
    m_idle = int(idle);
}

void QcMobCrowd::build(std::span<const QcCreature> mobs, const QList<QcMobDefinition> &definitions)
{
    // Emptied without giving back their storage: the crowd is counted anew every tick.
    m_cells.clear();
    m_cellIndex.removeIf([](const auto &) { return true; });
    m_cellIndex.reserve(std::ssize(mobs));
    m_members.assign(std::size_t(mobs.size()), Member{});

    for (qsizetype i = 0; i < std::ssize(mobs); ++i) {
        const QcCreature &mob = mobs[i];
        if (mob.dormant || !takesPart(mob, definitions[mob.type])) {
            continue;
        }

        const QcVec3 &at = mob.motion.position;
        const QcBlockPos pos{cellOf(at.x), cellOf(at.y + 0.01), cellOf(at.z)};
        int index = m_cellIndex.value(pos, -1);
        if (index < 0) {
            index = int(m_cells.size());
            m_cellIndex.insert(pos, index);
            m_cells.push_back({pos});
        }

        Cell &cell = m_cells[std::size_t(index)];
        const double halfWidth = definitions[mob.type].width * 0.5;
        cell.count += 1;
        cell.sumX += at.x;
        cell.sumZ += at.z;
        cell.sumHalfWidth += halfWidth;
        m_members[std::size_t(i)] = {index, mob.id, at.x, at.z, halfWidth};
    }
}

int QcMobCrowd::countAt(qsizetype index) const
{
    const Member &member = m_members[std::size_t(index)];
    return member.cell < 0 ? 0 : m_cells[std::size_t(member.cell)].count;
}

QcMobCrowd::Tier QcMobCrowd::tierAt(qsizetype index) const
{
    const int count = countAt(index);
    if (count >= m_idle) {
        return Tier::Idle;
    }
    return count >= m_throttled ? Tier::Throttled : Tier::Full;
}

int QcMobCrowd::thinkInterval(qsizetype index) const
{
    // Linear from the fewest ticks at the throttled threshold to the most at the idle one.
    const int span = std::max(1, m_idle - m_throttled);
    const int over = std::max(0, countAt(index) - m_throttled);
    return std::clamp(fewestThinkTicks + over * (mostThinkTicks - fewestThinkTicks) / span, fewestThinkTicks, mostThinkTicks);
}

QcVec3 QcMobCrowd::push(qsizetype index, const QcCreature &self, double halfWidth) const
{
    const Member &member = m_members[std::size_t(index)];
    if (member.cell < 0) {
        return {};
    }

    // Its own cell without itself, then the eight cells around it on the same level.
    const Cell &own = m_cells[std::size_t(member.cell)];
    QcVec3 total = pushFrom(self, halfWidth, own.count - 1, own, &member);
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            if (dx == 0 && dz == 0) {
                continue;
            }
            const auto found = m_cellIndex.constFind(own.pos.offset(dx, 0, dz));
            if (found != m_cellIndex.constEnd()) {
                const Cell &cell = m_cells[std::size_t(*found)];
                total += pushFrom(self, halfWidth, cell.count, cell, nullptr);
            }
        }
    }
    return total;
}

QcVec3 QcMobCrowd::pushFrom(const QcCreature &self, double halfWidth, int others, const Cell &sums, const Member *exclude)
{
    if (others <= 0) {
        return {};
    }

    // The centre and mean half width of the others.
    const double share = 1.0 / others;
    const double x = (sums.sumX - (exclude ? exclude->x : 0.0)) * share;
    const double z = (sums.sumZ - (exclude ? exclude->z : 0.0)) * share;
    const double otherHalfWidth = (sums.sumHalfWidth - (exclude ? exclude->halfWidth : 0.0)) * share;

    const double reach = halfWidth + otherHalfWidth;
    const QcVec3 apart{self.motion.position.x - x, 0.0, self.motion.position.z - z};
    const double distance = apart.length();
    if (distance >= reach) {
        return {};
    }
    const QcVec3 away = distance > 1e-4 ? apart * (1.0 / distance) : partingDirection(self.id, sums.pos);
    return away * (1.0 - distance / reach);
}

QList<QcMobCrowd::Crammed> QcMobCrowd::crammed(int limit) const
{
    QList<Crammed> crammed;
    if (limit <= 0) {
        return crammed;
    }
    for (std::size_t i = 0; i < m_members.size(); ++i) {
        const Member &member = m_members[i];
        if (member.cell >= 0 && m_cells[std::size_t(member.cell)].count > limit) {
            crammed.append({member.id, qsizetype(i), member.cell, m_cells[std::size_t(member.cell)].count});
        }
    }
    return crammed;
}

int QcMobCrowd::largestCell() const
{
    int largest = 0;
    for (const Cell &cell : m_cells) {
        largest = std::max(largest, cell.count);
    }
    return largest;
}
