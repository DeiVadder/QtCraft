#include "common/gameplay/QcExperience.h"

#include <algorithm>
#include <array>
#include <cmath>

QcExperience::QcExperience(int level, int points)
    : m_level(std::clamp(level, 0, maxLevel))
    , m_points(std::clamp(points, 0, mostPoints(m_level)))
{
}

int QcExperience::mostPoints(int level)
{
    // The highest level is the end of the way: no points towards a next one.
    return level >= maxLevel ? 0 : pointsForNext(level) - 1;
}

int QcExperience::pointsForNext(int level)
{
    if (level >= 30) {
        return 112 + (level - 30) * 9;
    }
    if (level >= 15) {
        return 37 + (level - 15) * 5;
    }
    return 7 + std::max(0, level) * 2;
}

qint64 QcExperience::totalForLevel(int level)
{
    // The sums of pointsForNext in closed form; the halves cancel for every level.
    const qint64 l = std::clamp(level, 0, maxLevel);
    if (l <= 16) {
        return l * l + 6 * l;
    }
    if (l <= 31) {
        return (5 * l * l - 81 * l) / 2 + 360;
    }
    return (9 * l * l - 325 * l) / 2 + 2220;
}

QcExperience QcExperience::fromTotal(qint64 total)
{
    const qint64 wanted = std::clamp<qint64>(total, 0, totalForLevel(maxLevel));

    // The level is the largest whose start is not beyond the total.
    int low = 0;
    int high = maxLevel;
    while (low < high) {
        const int middle = (low + high + 1) / 2;
        if (totalForLevel(middle) <= wanted) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }

    QcExperience experience;
    experience.m_level = low;
    experience.m_points = low == maxLevel ? 0 : int(wanted - totalForLevel(low));
    return experience;
}

QList<int> QcExperience::orbValues(int points, int maxOrbs)
{
    static constexpr std::array<int, 11> sizes{2477, 1237, 617, 307, 149, 73, 37, 17, 7, 3, 1};

    QList<int> values;
    int left = std::max(0, points);
    while (left > 0 && values.size() < maxOrbs) {
        const int size = *std::ranges::find_if(sizes, [left](int candidate) { return candidate <= left; });
        values.append(size);
        left -= size;
    }
    if (left > 0 && !values.isEmpty()) {
        values.last() += left;
    }
    return values;
}

int QcExperience::addPoints(qint64 points)
{
    const int before = m_level;
    *this = fromTotal(total() + points);
    return m_level - before;
}

void QcExperience::addLevels(int levels)
{
    setLevel(int(std::clamp<qint64>(qint64(m_level) + levels, 0, maxLevel)));
}

bool QcExperience::spendLevels(int levels)
{
    if (levels < 0 || levels > m_level) {
        return false;
    }

    addLevels(-levels);
    return true;
}

void QcExperience::setLevel(int level)
{
    // Like the reference, the share of the way to the next level stays.
    const double share = progress();
    m_level = std::clamp(level, 0, maxLevel);
    m_points = std::clamp(int(std::floor(share * pointsForNext(m_level))), 0, mostPoints(m_level));
}

bool QcExperience::setPoints(int points)
{
    if (points < 0 || points > mostPoints(m_level)) {
        return false;
    }

    m_points = points;
    return true;
}

int QcExperience::deathDrop() const
{
    return std::min(maxDeathPoints, m_level * deathPointsPerLevel);
}
