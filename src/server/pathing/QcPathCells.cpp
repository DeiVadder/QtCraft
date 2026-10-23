#include "server/pathing/QcPathCells.h"

#include "common/content/registry/QcContentRegistry.h"

#include <algorithm>
#include <cmath>

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

} // namespace

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

    // Doors, gates and trapdoors (blocks with an `open` state): open ones let bodies through.
    const bool opens = block.property(u"open"_s) != nullptr;
    if (opens && content.property(id, u"open"_s) == u"true"_s) {
        return make(Kind::Open, flags);
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
    for (qsizetype id = 0; id < content.stateCount(); ++id) {
        m_cells[static_cast<std::size_t>(id)] = QcPathCell::classify(content, static_cast<QcBlockId>(id));
    }
}
