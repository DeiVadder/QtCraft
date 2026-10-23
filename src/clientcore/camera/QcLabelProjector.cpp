#include "clientcore/camera/QcLabelProjector.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcBlockPos.h"

#include <QMatrix4x4>
#include <QVector4D>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace {

// Only the depth range of the projection depends on it; positions on screen do not.
constexpr float projectionFar = 1000.0f;
// Labels a little outside the view are dropped (their text would hang off the edge anyway).
constexpr double viewMargin = 1.05;
// Labels fade out over the last part of their reach.
constexpr double fadeShare = 0.25;

struct Candidate
{
    const QcWorldLabel *label = nullptr;
    quint64 key = 0;
    double distance = 0.0;
};

// The labels near the eye, gathered chunk by chunk into one list.
class Candidates
{
public:
    // Adds the labels of the chunk at `pos` that are within their reach of the eye.
    void addChunk(QcChunkPos pos, const QList<QcWorldLabel> &labels, const QcVec3 &eye)
    {
        const quint64 chunkKey = (static_cast<quint64>(static_cast<quint32>(pos.x)) << 40) ^ (static_cast<quint64>(static_cast<quint32>(pos.z)) << 16);
        for (qsizetype i = 0; i < labels.size(); ++i) {
            const QcWorldLabel &label = labels[i];
            const double distance = (label.position - eye).length();
            if (distance <= (label.heading ? QcLabelProjector::headingReach : QcLabelProjector::labelReach)) {
                m_list.append({&label, chunkKey ^ static_cast<quint64>(i), distance});
            }
        }
    }

    [[nodiscard]] QList<Candidate> take() { return std::exchange(m_list, {}); }

private:
    QList<Candidate> m_list;
};

[[nodiscard]] QList<Candidate> candidatesNear(const QHash<QcChunkPos, QList<QcWorldLabel>> &labels, const QcVec3 &eye)
{
    Candidates candidates;
    const QcChunkPos centre = QcBlockPos{static_cast<int>(std::floor(eye.x)), 0, static_cast<int>(std::floor(eye.z))}.chunk();
    const int radius = static_cast<int>(std::ceil(QcLabelProjector::headingReach / QcWorld::chunkSize)) + 1;
    for (int dx = -radius; dx <= radius; ++dx) {
        for (int dz = -radius; dz <= radius; ++dz) {
            const QcChunkPos pos{centre.x + dx, centre.z + dz};
            if (const auto found = labels.constFind(pos); found != labels.constEnd()) {
                candidates.addChunk(pos, *found, eye);
            }
        }
    }
    return candidates.take();
}

// Steps of a voxel traversal (Amanatides & Woo) along one axis.
struct AxisWalk
{
    int step = 0;
    // Ray length to the next cell border on this axis and between two borders.
    double next = std::numeric_limits<double>::infinity();
    double delta = std::numeric_limits<double>::infinity();
};

AxisWalk axisWalk(double origin, double direction, int cell)
{
    AxisWalk walk;
    if (direction > 0.0) {
        walk = {1, (cell + 1 - origin) / direction, 1.0 / direction};
    } else if (direction < 0.0) {
        walk = {-1, (origin - cell) / -direction, -1.0 / direction};
    }
    return walk;
}

// Whether the block at `pos` hides what lies behind it; unloaded cells count as hiding nothing.
bool hidesAt(const QcBlockAccess &world, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = world.blockAt(pos);
    return block && world.content().state(*block).opaque;
}

// `focal`: pixels per metre at a depth of one metre.
[[nodiscard]] std::optional<QcScreenLabel> project(const Candidate &candidate, const QMatrix4x4 &viewProjection, double focal, const QcLabelProjector::View &view)
{
    const QcVec3 &p = candidate.label->position;
    const QVector4D clip = viewProjection * QVector4D(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z), 1.0f);
    if (clip.w() <= QcCamera::nearPlane) {
        return std::nullopt;
    }

    const double ndcX = clip.x() / clip.w();
    const double ndcY = clip.y() / clip.w();
    if (std::abs(ndcX) > viewMargin || std::abs(ndcY) > viewMargin) {
        return std::nullopt;
    }

    const double reach = candidate.label->heading ? QcLabelProjector::headingReach : QcLabelProjector::labelReach;
    const double fadeStart = reach * (1.0 - fadeShare);
    QcScreenLabel screen;
    screen.key = candidate.key;
    screen.text = candidate.label->text;
    screen.detail = candidate.label->detail;
    screen.heading = candidate.label->heading;
    screen.x = (ndcX + 1.0) * 0.5 * view.width;
    screen.y = (1.0 - ndcY) * 0.5 * view.height;
    screen.distance = candidate.distance;
    screen.scale = std::clamp(4.0 / std::max(candidate.distance, 0.1), 0.6, 1.3);
    screen.opacity = std::clamp(1.0 - (candidate.distance - fadeStart) / (reach - fadeStart), 0.0, 1.0);
    screen.maxWidth = QcLabelProjector::labelSpacing * focal / clip.w() / screen.scale;
    return screen;
}

} // namespace

bool QcLabelProjector::isHidden(const QcBlockAccess &world, const QcVec3 &eye, const QcVec3 &point)
{
    const QcVec3 toPoint = point - eye;
    const double distance = toPoint.length();
    if (distance < 0.5) {
        return false;
    }

    // Every cell the line of sight passes, up to just before the label: only opaque full blocks
    // hide it, glass, panes and leaves on the way do not, whatever lies behind them.
    const QcVec3 direction = toPoint * (1.0 / distance);
    const double end = distance - 0.3;
    QcBlockPos cell{static_cast<int>(std::floor(eye.x)), static_cast<int>(std::floor(eye.y)), static_cast<int>(std::floor(eye.z))};
    std::array<AxisWalk, 3> walks{axisWalk(eye.x, direction.x, cell.x), axisWalk(eye.y, direction.y, cell.y),
                                  axisWalk(eye.z, direction.z, cell.z)};
    for (double travelled = 0.0; travelled <= end;) {
        if (hidesAt(world, cell)) {
            return true;
        }

        // On to the neighbour across the nearest cell border.
        AxisWalk &nearest = *std::ranges::min_element(walks, {}, &AxisWalk::next);
        const qsizetype axis = &nearest - walks.data();
        travelled = nearest.next;
        nearest.next += nearest.delta;
        cell = cell.offset(axis == 0 ? nearest.step : 0, axis == 1 ? nearest.step : 0, axis == 2 ? nearest.step : 0);
    }
    return false;
}

QList<QcScreenLabel> QcLabelProjector::visible(const QHash<QcChunkPos, QList<QcWorldLabel>> &labels, const View &view, const QcBlockAccess *world)
{
    if (view.width <= 0.0 || view.height <= 0.0) {
        return {};
    }

    const QcVec3 eye{view.camera.position.x(), view.camera.position.y(), view.camera.position.z()};
    QList<Candidate> candidates = candidatesNear(labels, eye);
    std::ranges::sort(candidates, {}, &Candidate::distance);

    const QMatrix4x4 projection = view.camera.projectionMatrix(static_cast<float>(view.width / view.height), projectionFar);
    const QMatrix4x4 viewProjection = projection * view.camera.viewMatrix();
    const double focal = 0.5 * view.height * projection(1, 1);
    QList<QcScreenLabel> shown;
    int rays = 0;
    for (const Candidate &candidate : std::as_const(candidates)) {
        // Nearest first; once the budget of sight rays is spent, farther labels stay hidden.
        if (shown.size() >= maxShown || (world && rays >= maxSightRays)) {
            break;
        }
        std::optional<QcScreenLabel> screen = project(candidate, viewProjection, focal, view);
        if (!screen || screen->opacity <= 0.0) {
            continue;
        }

        if (world) {
            ++rays;
            if (isHidden(*world, eye, candidate.label->position)) {
                continue;
            }
        }
        shown.append(std::move(*screen));
    }

    std::ranges::reverse(shown);
    return shown;
}
