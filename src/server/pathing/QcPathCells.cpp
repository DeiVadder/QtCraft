#include "server/pathing/QcPathCells.h"

#include "common/content/registry/QcContentRegistry.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

using namespace Qt::StringLiterals;

namespace QcPathCell {

namespace {

// Collision covering less than this share of the cell's ground is no floor to stand on and no full
// wall: ladders, panes, iron bars, doors and gates.
constexpr double thinFootprint = 0.5;

// Highest point of the state's collision boxes inside its cell (up to 1.5 for fences); 0 without boxes.
double collisionTop(const QcBlockState &state)
{
    double top = 0.0;
    for (const QcAabb &box : state.collision) {
        top = std::max(top, box.max.y);
    }
    return top;
}

// Share of the cell's ground the collision boxes cover, seen from above (the boxes' areas summed, at most 1).
double footprint(const QcBlockState &state)
{
    double area = 0.0;
    for (const QcAabb &box : state.collision) {
        area += (box.max.x - box.min.x) * (box.max.z - box.min.z);
    }
    return std::min(1.0, area);
}

// A low block with the height of its top in eighths, rounded up (a carpet 1, a slab 4).
quint8 lowCell(double top, quint8 flags)
{
    const int eighths = std::clamp(static_cast<int>(std::ceil(top * 8.0 - 1e-6)), 1, 4);
    return make(Kind::Low, static_cast<quint8>(flags | static_cast<quint8>((eighths - 1) << liftShift)));
}

// Whether the state is the open state of a door, gate or trapdoor (a block with an `open` state).
bool isOpen(const QcContentRegistry &content, QcBlockId id)
{
    return content.block(id).property(u"open"_s) != nullptr && content.property(id, u"open"_s) == u"true"_s;
}

// Panels reaching no higher than this above the cell's bottom are stepped over, not walked beside.
constexpr double panelStep = 0.5;

// The widest stretch of the cell along x (`alongX`) or z that no panel covers: a panel is a
// collision box over the whole cell along the other axis that reaches above panelStep (the leaf of
// an open door, an open trapdoor). None without a panel or without a free stretch. Panels of the
// same opening in other cells (the cell above, the next door) are combined by the navigator
// (QcPathGap::within), not here.
std::optional<QcPathOpening> opening(const QList<QcAabb> &boxes, bool alongX)
{
    constexpr double edge = 1e-6;
    std::vector<std::pair<double, double>> panels;
    for (const QcAabb &box : boxes) {
        const double acrossMin = alongX ? box.min.z : box.min.x;
        const double acrossMax = alongX ? box.max.z : box.max.x;
        if (acrossMin <= edge && acrossMax >= 1.0 - edge && box.max.y > panelStep) {
            panels.emplace_back(std::clamp(alongX ? box.min.x : box.min.z, 0.0, 1.0), std::clamp(alongX ? box.max.x : box.max.z, 0.0, 1.0));
        }
    }
    if (panels.empty()) {
        return std::nullopt;
    }

    std::ranges::sort(panels);
    double from = 0.0;
    double best = 0.0;
    std::optional<QcPathOpening> widest;
    const auto consider = [&](double to) {
        if (to - from > best) {
            best = to - from;
            widest = QcPathOpening{static_cast<float>(from), static_cast<float>(to)};
        }
    };
    for (const auto &[low, high] : panels) {
        consider(low);
        from = std::max(from, high);
    }
    consider(1.0);
    return widest;
}

} // namespace

QcPathGap gapOf(const QcContentRegistry &content, QcBlockId id)
{
    if (!isOpen(content, id)) {
        return {};
    }
    const QList<QcAabb> &collision = content.state(id).collision;
    return {opening(collision, true), opening(collision, false)};
}

quint8 classify(const QcContentRegistry &content, QcBlockId id)
{
    const QcBlockDefinition &block = content.block(id);
    const QcBlockState &state = content.state(id);
    const bool hurts = block.contactDamage > 0.0 || block.fire.isFire || block.fire.setsAlightSeconds > 0;
    quint8 flags = hurts ? hurtsFlag : quint8{0};
    if (block.slowsInside < 1.0) {
        flags |= slowsFlag;
    }

    if (block.liquid) {
        return hurts ? make(Kind::Danger, flags) : make(Kind::Water, flags);
    }

    // Doors, gates and trapdoors (blocks with an `open` state): open ones let bodies through, beside
    // their panel (decision E-909) if they have one.
    const bool opens = block.property(u"open"_s) != nullptr;
    if (isOpen(content, id)) {
        return make(Kind::Open, gapOf(content, id).narrows() ? static_cast<quint8>(flags | panelFlag) : flags);
    }
    if (state.collision.isEmpty()) {
        return make(hurts ? Kind::Danger : Kind::Open, flags);
    }

    // Closed upright doors and gates: a hand opens the wooden ones, a signal the iron ones (a wall
    // to every creature). Closed trapdoors lie flat and count by their height below.
    const double top = collisionTop(state);
    const bool thin = footprint(state) < thinFootprint;
    if (opens && thin) {
        return make(block.toggles ? Kind::ClosedDoor : Kind::Wall, flags);
    }
    if (top > 1.0) {
        return make(Kind::Tall, flags);
    }
    // A ladder's board leaves room for a body in its cell; a pane or bars in the middle do not.
    if (thin) {
        return make(block.climbable ? Kind::Ladder : Kind::Wall, flags);
    }
    if (top <= 0.5) {
        return lowCell(top, flags);
    }
    // A slippery floor (ice, decision E-430) carries its flag where low cells keep their height.
    return make(Kind::Solid, block.slipperiness > QcBlockDefinition::grippingSlipperiness ? static_cast<quint8>(flags | slipperyFlag) : flags);
}

} // namespace QcPathCell

QcPathCellTable::QcPathCellTable(const QcContentRegistry &content)
{
    m_cells.resize(static_cast<std::size_t>(content.stateCount()));
    m_gaps.resize(static_cast<std::size_t>(content.stateCount()));
    for (qsizetype id = 0; id < content.stateCount(); ++id) {
        m_cells[static_cast<std::size_t>(id)] = QcPathCell::classify(content, static_cast<QcBlockId>(id));
        m_gaps[static_cast<std::size_t>(id)] = QcPathCell::gapOf(content, static_cast<QcBlockId>(id));
    }
}
