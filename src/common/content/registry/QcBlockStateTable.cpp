#include "common/content/registry/QcBlockStateTable.h"

#include "common/world/QcLightPassage.h"

#include <QHash>
#include <QSet>
#include <QVarLengthArray>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

constexpr QcAabb unitBox{{0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}};

// Turns of an authored shape (facing north) for a facing value: quarter turns clockwise seen from
// above for east, south and west, a tilt that points north at the sky (up) or the ground (down) for
// pistons (decision E-334).
constexpr int turnUp = 4;
constexpr int turnDown = 5;

int quarterTurns(const QString &value)
{
    if (value == u"east"_s) {
        return 1;
    }
    if (value == u"south"_s) {
        return 2;
    }
    if (value == u"west"_s) {
        return 3;
    }
    if (value == u"up"_s) {
        return turnUp;
    }
    if (value == u"down"_s) {
        return turnDown;
    }
    return 0;
}

QcAabb turned(const QcAabb &box, int turns)
{
    if (turns == turnUp) {
        // (y, z) -> (1 - z, y): north (-z) becomes up (+y).
        return {{box.min.x, 1.0 - box.max.z, box.min.y}, {box.max.x, 1.0 - box.min.z, box.max.y}};
    }
    if (turns == turnDown) {
        // (y, z) -> (z, 1 - y): north (-z) becomes down (-y).
        return {{box.min.x, box.min.z, 1.0 - box.max.y}, {box.max.x, box.max.z, 1.0 - box.min.y}};
    }

    QcAabb result = box;
    for (int i = 0; i < turns; ++i) {
        // (x, z) -> (1 - z, x): north (-z) becomes east (+x).
        result = {{1.0 - result.max.z, result.min.y, result.min.x}, {1.0 - result.min.z, result.max.y, result.max.x}};
    }
    return result;
}

QcAabb flipped(const QcAabb &box)
{
    return {{box.min.x, 1.0 - box.max.y, box.min.z}, {box.max.x, 1.0 - box.min.y, box.max.z}};
}

QcBlockFace turnedFace(QcBlockFace face, int turns)
{
    if (turns == turnUp || turns == turnDown) {
        // Tilting about the x axis: north, up, south and down follow each other.
        constexpr std::array<QcBlockFace, 4> tilt = {QcBlockFace::North, QcBlockFace::Up, QcBlockFace::South, QcBlockFace::Down};
        const auto it = std::ranges::find(tilt, face);
        const std::size_t step = turns == turnUp ? 1 : 3;
        return it == tilt.end() ? face : tilt[(std::size_t(it - tilt.begin()) + step) % tilt.size()];
    }

    constexpr std::array<QcBlockFace, 4> ring = {QcBlockFace::North, QcBlockFace::East, QcBlockFace::South, QcBlockFace::West};
    for (std::size_t i = 0; i < ring.size(); ++i) {
        if (ring[i] == face) {
            return ring[(i + std::size_t(turns)) % ring.size()];
        }
    }
    return face;
}

double low(const QcAabb &box, int axis)
{
    return axis == 0 ? box.min.x : (axis == 1 ? box.min.y : box.min.z);
}

double high(const QcAabb &box, int axis)
{
    return axis == 0 ? box.max.x : (axis == 1 ? box.max.y : box.max.z);
}

// A box face on a cell side, in the side's two axes A and B.
struct SideRect
{
    double a0, a1, b0, b1;
};

using SideRects = QVarLengthArray<SideRect, 16>;
using SideCuts = QVarLengthArray<double, 34>;

// The faces of boxes lying on one cell side, and their edges along axis A (with 0 and 1).
struct Side
{
    SideRects rects;
    SideCuts cuts{0.0, 1.0};
};

[[nodiscard]] Side sideOf(const QList<QcAabb> &boxes, QcBlockFace face)
{
    Side side;
    const QcFaceOffset offset = qcFaceOffset(face);
    const int normal = offset.dx != 0 ? 0 : (offset.dy != 0 ? 1 : 2);
    const int axisA = normal == 0 ? 1 : 0;
    const int axisB = normal == 2 ? 1 : 2;
    const bool positive = offset.dx + offset.dy + offset.dz > 0;

    for (const QcAabb &box : boxes) {
        if (positive ? high(box, normal) >= 1.0 : low(box, normal) <= 0.0) {
            const SideRect rect{std::clamp(low(box, axisA), 0.0, 1.0), std::clamp(high(box, axisA), 0.0, 1.0),
                                std::clamp(low(box, axisB), 0.0, 1.0), std::clamp(high(box, axisB), 0.0, 1.0)};
            side.rects.append(rect);
            side.cuts.append(rect.a0);
            side.cuts.append(rect.a1);
        }
    }
    return side;
}

// Whether the rectangles spanning the strip [from, to] along axis A cover [0, 1] along axis B.
bool stripCovered(const SideRects &onSide, double from, double to)
{
    QVarLengthArray<std::pair<double, double>, 16> spans;
    for (const SideRect &rect : onSide) {
        if (rect.a0 <= from && rect.a1 >= to) {
            spans.append({rect.b0, rect.b1});
        }
    }

    std::ranges::sort(spans);
    double reached = 0.0;
    for (const auto &[spanFrom, spanTo] : spans) {
        if (spanFrom > reached) {
            break;
        }
        reached = std::max(reached, spanTo);
    }

    return reached >= 1.0;
}

// Whether the faces of `boxes` lying on the cell side `face` cover that side completely together:
// for every strip between neighbouring box edges along one axis, the boxes spanning the strip must
// cover [0, 1] along the other axis.
bool coversSide(const QList<QcAabb> &boxes, QcBlockFace face)
{
    Side side = sideOf(boxes, face);
    const SideRects &onSide = side.rects;
    SideCuts &cuts = side.cuts;
    if (onSide.isEmpty()) {
        return false;
    }

    std::ranges::sort(cuts);
    for (qsizetype i = 0; i + 1 < cuts.size(); ++i) {
        if (cuts[i] != cuts[i + 1] && !stripCovered(onSide, cuts[i], cuts[i + 1])) {
            return false;
        }
    }
    return true;
}

// Identifies the variant of a state: turns (bits 32-34), flip (bit 35) and the parts that apply
// (bits 0-31). States of one block with the same key derive the same data.
quint64 variantKey(const QcBlockDefinition &block, const QcShapeDefinition &shape, quint32 index)
{
    const int turns = shape.rotate.isEmpty() ? 0 : quarterTurns(QcBlockStateTable::value(block, index, shape.rotate));
    const bool flip = !shape.flip.isEmpty() && QcBlockStateTable::value(block, index, shape.flip) == u"top"_s;
    quint32 mask = 0;
    for (qsizetype part = 0; part < shape.parts.size() && part < 32; ++part) {
        const bool applies = std::ranges::all_of(shape.parts[part].when, [&](const QcShapeCondition &condition) {
            return condition.values.contains(QcBlockStateTable::value(block, index, condition.property));
        });
        if (applies) {
            mask |= quint32(1) << part;
        }
    }

    return quint64(mask) | (quint64(turns) << 32) | (quint64(flip ? 1 : 0) << 35);
}

// State `index` with the boxes of the parts that apply to it, turned and flipped as its properties say.
[[nodiscard]] QcBlockState shapedState(const QcBlockDefinition &block, const QcShapeDefinition &shape, quint32 index)
{
    QcBlockState state;
    state.index = index;
    const int turns = shape.rotate.isEmpty() ? 0 : quarterTurns(QcBlockStateTable::value(block, index, shape.rotate));
    const bool flip = !shape.flip.isEmpty() && QcBlockStateTable::value(block, index, shape.flip) == u"top"_s;
    const auto place = [turns, flip](const QcAabb &box) { return turned(flip ? flipped(box) : box, turns); };

    for (const QcShapePart &part : shape.parts) {
        const bool applies = std::ranges::all_of(part.when, [&](const QcShapeCondition &condition) {
            return condition.values.contains(QcBlockStateTable::value(block, index, condition.property));
        });
        if (!applies) {
            continue;
        }

        for (const QcAabb &box : part.boxes) {
            state.boxes.append(place(box));
            state.boxTextures.append(part.texture);
            state.boxStretch.append(part.stretch);
        }
        for (const QcAabb &box : part.crosses) {
            state.crosses.append(place(box));
            state.crossTextures.append(part.texture);
        }
        for (const QcAabb &box : part.customCollision ? part.collision : part.boxes) {
            state.collision.append(place(box));
        }
    }

    for (const QcBlockFace face : qcAllBlockFaces) {
        state.faceSource[std::size_t(turnedFace(face, turns))] = face;
    }
    return state;
}

// Characters that structure saved state names ("name[a=x,b=y]").
bool hasSeparator(const QString &text)
{
    return text.contains(u',') || text.contains(u'=') || text.contains(u'[') || text.contains(u']');
}

} // namespace

qsizetype QcBlockStateTable::stateCountOf(const QcBlockDefinition &block)
{
    qsizetype count = 1;
    for (const QcBlockProperty &property : block.properties) {
        count *= property.values.size();
        if (count == 0 || count > maxStatesPerBlock) {
            return count;
        }
    }
    return count;
}

QString QcBlockStateTable::problem(const QcBlockDefinition &block)
{
    if (block.properties.size() > QcBlockProperty::maxProperties) {
        return u"at most %1 state properties"_s.arg(QcBlockProperty::maxProperties);
    }

    QSet<QString> names;
    for (const QcBlockProperty &property : block.properties) {
        if (property.name.isEmpty() || property.name.size() > 32 || names.contains(property.name) || hasSeparator(property.name)) {
            return u"state property names must be unique, 1..32 characters and free of '=', ',', '[' and ']'"_s;
        }
        names.insert(property.name);

        if (property.values.isEmpty() || property.values.size() > QcBlockProperty::maxValues
            || QSet<QString>(property.values.begin(), property.values.end()).size() != property.values.size()) {
            return u"state property '%1' needs 1..%2 distinct values"_s.arg(property.name).arg(QcBlockProperty::maxValues);
        }
        for (const QString &value : property.values) {
            if (value.isEmpty() || value.size() > 32 || hasSeparator(value)) {
                return u"state property '%1' has an invalid value"_s.arg(property.name);
            }
        }
    }

    const qsizetype count = stateCountOf(block);
    if (count < 1 || count > maxStatesPerBlock) {
        return u"at most %1 states per block"_s.arg(maxStatesPerBlock);
    }
    return {};
}

qsizetype QcBlockStateTable::derivedBoxes(const QcBlockDefinition &block, const QcShapeDefinition &shape)
{
    if (shape.isEmpty()) {
        return 1;
    }

    const auto count = quint32(std::max<qsizetype>(1, std::min<qsizetype>(stateCountOf(block), maxStatesPerBlock)));
    QSet<quint64> seen;
    qsizetype boxes = 0;

    for (quint32 index = 0; index < count; ++index) {
        const quint64 key = variantKey(block, shape, index);
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key);

        const quint32 mask = quint32(key);
        for (qsizetype part = 0; part < shape.parts.size(); ++part) {
            if (mask & (quint32(1) << part)) {
                const QcShapePart &applied = shape.parts[part];
                boxes += applied.boxes.size() + applied.crosses.size() + (applied.customCollision ? applied.collision.size() : applied.boxes.size());
            }
        }
    }
    return std::max<qsizetype>(1, boxes);
}

bool QcBlockStateTable::fits(std::optional<quint32> type, qsizetype boxes) const
{
    const qsizetype replaced = type && qsizetype(*type) < m_boxes.size() ? m_boxes[qsizetype(*type)] : 0;
    return m_totalBoxes - replaced + boxes <= maxDerivedBoxes;
}

void QcBlockStateTable::clear()
{
    m_states.clear();
    m_firstState.clear();
    m_stateCounts.clear();
    m_boxes.clear();
    m_totalBoxes = 0;
}

QList<QcBlockState> QcBlockStateTable::computeAll(const QcBlockDefinition &block, const QcShapeDefinition &shape, int count, quint32 type)
{
    QList<QcBlockState> states;
    states.reserve(count);
    QHash<quint64, qsizetype> variants;

    for (int index = 0; index < count; ++index) {
        const quint64 key = variantKey(block, shape, quint32(index));
        const auto variant = variants.constFind(key);
        if (variant != variants.constEnd()) {
            // Same boxes, faces and flags: share the lists of the first state of this variant.
            QcBlockState state = states[*variant];
            state.index = quint32(index);
            states.append(std::move(state));
            continue;
        }

        variants.insert(key, states.size());
        QcBlockState state = compute(block, shape, quint32(index));
        state.type = type;
        states.append(std::move(state));
    }

    return states;
}

void QcBlockStateTable::append(const QcBlockDefinition &block, const QcShapeDefinition &shape)
{
    const auto type = quint32(m_firstState.size());
    const auto count = int(std::max<qsizetype>(1, stateCountOf(block)));
    m_firstState.append(quint32(m_states.size()));
    m_stateCounts.append(count);

    const qsizetype boxes = derivedBoxes(block, shape);
    m_boxes.append(boxes);
    m_totalBoxes += boxes;

    m_states.append(computeAll(block, shape, count, type));
}

void QcBlockStateTable::update(quint32 type, const QcBlockDefinition &block, const QcShapeDefinition &shape)
{
    const quint32 first = m_firstState[qsizetype(type)];
    const int count = m_stateCounts[qsizetype(type)];

    const qsizetype boxes = derivedBoxes(block, shape);
    m_totalBoxes += boxes - m_boxes[qsizetype(type)];
    m_boxes[qsizetype(type)] = boxes;

    QList<QcBlockState> states = computeAll(block, shape, count, type);
    for (int index = 0; index < count; ++index) {
        m_states[qsizetype(first) + index] = std::move(states[index]);
    }
}

QString QcBlockStateTable::value(const QcBlockDefinition &block, quint32 index, const QString &property)
{
    quint32 rest = index;
    for (const QcBlockProperty &candidate : block.properties) {
        const auto count = quint32(candidate.values.size());
        if (candidate.name == property) {
            return candidate.values[qsizetype(rest % count)];
        }
        rest /= count;
    }
    return {};
}

std::optional<quint32> QcBlockStateTable::withValue(const QcBlockDefinition &block, quint32 index, const QString &property,
                                                    const QString &value)
{
    quint32 stride = 1;
    for (const QcBlockProperty &candidate : block.properties) {
        const auto count = quint32(candidate.values.size());
        if (candidate.name == property) {
            const qsizetype wanted = candidate.values.indexOf(value);
            if (wanted < 0) {
                return std::nullopt;
            }
            const quint32 current = (index / stride) % count;
            return index - current * stride + quint32(wanted) * stride;
        }
        stride *= count;
    }
    return std::nullopt;
}

QString QcBlockStateTable::describe(const QcBlockDefinition &block, quint32 index)
{
    if (block.properties.isEmpty()) {
        return {};
    }

    QStringList parts;
    quint32 rest = index;
    for (const QcBlockProperty &property : block.properties) {
        const auto count = quint32(property.values.size());
        parts.append(property.name + u'=' + property.values[qsizetype(rest % count)]);
        rest /= count;
    }
    return u'[' + parts.join(u',') + u']';
}

QcBlockState QcBlockStateTable::compute(const QcBlockDefinition &block, const QcShapeDefinition &shape, quint32 index)
{
    QcBlockState state = shape.isEmpty() ? QcBlockState{} : shapedState(block, shape, index);
    state.index = index;
    if (shape.isEmpty()) {
        state.boxes = {unitBox};
        state.boxTextures = {QString()};
        state.boxStretch = {false};
    }

    if (shape.isEmpty()) {
        state.collision = {unitBox};
    }
    if (!block.solid) {
        state.collision.clear();
    }

    state.fullCube = state.boxes.size() == 1 && state.boxes.constFirst() == unitBox && state.crosses.isEmpty();
    state.opaque = block.opaque && state.fullCube && block.renderLayer == QcRenderLayer::Opaque;
    state.lightOpacity = block.lightOpacity;

    for (const QcBlockFace face : qcAllBlockFaces) {
        state.fullFace[std::size_t(face)] = coversSide(state.boxes, face);
    }

    // A partial shape of a light-dimming block (stairs, slabs) dims light by its covered faces only.
    if (!state.fullCube && state.lightOpacity > 0) {
        state.lightOpacity = 0;
        for (const QcBlockFace face : qcAllBlockFaces) {
            state.lightBlockedFaces |= state.fullFace[std::size_t(face)] ? QcLightPassage::bit(face) : quint8(0);
        }
    }
    return state;
}
