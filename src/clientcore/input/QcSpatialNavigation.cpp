#include "clientcore/input/QcSpatialNavigation.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace QcSpatialNavigation {

namespace {

// How far `candidate` lies beyond `from` along the direction (its centre beyond `from`'s centre),
// and the gap between the facing edges.
struct Along
{
    double centre = 0.0;
    double gap = 0.0;
};

Along along(const QRectF &from, const QRectF &candidate, Direction direction)
{
    switch (direction) {
    case Direction::Up:
        return {from.center().y() - candidate.center().y(), from.top() - candidate.bottom()};
    case Direction::Down:
        return {candidate.center().y() - from.center().y(), candidate.top() - from.bottom()};
    case Direction::Left:
        return {from.center().x() - candidate.center().x(), from.left() - candidate.right()};
    case Direction::Right:
        return {candidate.center().x() - from.center().x(), candidate.left() - from.right()};
    }
    return {};
}

// The distance across the direction: zero while the two overlap on that axis.
double across(const QRectF &from, const QRectF &candidate, Direction direction)
{
    const bool vertical = direction == Direction::Up || direction == Direction::Down;
    const double fromLow = vertical ? from.left() : from.top();
    const double fromHigh = vertical ? from.right() : from.bottom();
    const double low = vertical ? candidate.left() : candidate.top();
    const double high = vertical ? candidate.right() : candidate.bottom();
    return std::max({0.0, low - fromHigh, fromLow - high});
}

} // namespace

std::optional<qsizetype> next(const QRectF &from, Direction direction, const QList<QRectF> &candidates)
{
    std::optional<qsizetype> best;
    double bestCost = std::numeric_limits<double>::infinity();
    for (qsizetype index = 0; index < candidates.size(); ++index) {
        const QRectF &candidate = candidates[index];
        const Along distance = along(from, candidate, direction);
        const double offset = across(from, candidate, direction);
        // Beyond the centre by at least a pixel (an item at the same place never counts), and
        // further along the direction than off to the side.
        if (!(distance.centre >= 1.0) || offset > distance.centre) {
            continue;
        }

        const double cost = std::max(0.0, distance.gap) + 2.0 * offset;
        if (cost < bestCost) {
            bestCost = cost;
            best = index;
        }
    }
    return best;
}

std::optional<qsizetype> first(const QList<QRectF> &candidates)
{
    std::optional<qsizetype> best;
    for (qsizetype index = 0; index < candidates.size(); ++index) {
        if (!best) {
            best = index;
            continue;
        }

        const QRectF &candidate = candidates[index];
        const QRectF &current = candidates[*best];
        const double tolerance = std::min(candidate.height(), current.height()) / 2.0;
        const double rowDifference = candidate.center().y() - current.center().y();
        const bool higher = rowDifference < -tolerance;
        const bool sameRowFurtherLeft = std::abs(rowDifference) <= tolerance && candidate.center().x() < current.center().x();
        if (higher || sameRowFurtherLeft) {
            best = index;
        }
    }
    return best;
}

} // namespace QcSpatialNavigation
