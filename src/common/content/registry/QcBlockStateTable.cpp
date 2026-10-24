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
// Turns for the state `axis` (decision E-751): a block authored along y lies along x (a roll that
// takes up to east) or along z (the tilt of turnUp).
constexpr int turnAxisX = 6;
constexpr int turnAxisZ = 7;

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
    if (value == u"x"_s) {
        return turnAxisX;
    }
    if (value == u"z"_s) {
        return turnAxisZ;
    }
    return 0;
}

QcAabb turned(const QcAabb &box, int turns)
{
    if (turns == turnAxisX) {
        // (x, y) -> (y, 1 - x): up (+y) becomes east (+x).
        return {{box.min.y, 1.0 - box.max.x, box.min.z}, {box.max.y, 1.0 - box.min.x, box.max.z}};
    }
    if (turns == turnUp || turns == turnAxisZ) {
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
    if (turns == turnAxisX) {
        // Rolling about the z axis: up, east, down and west follow each other.
        constexpr std::array<QcBlockFace, 4> roll = {QcBlockFace::Up, QcBlockFace::East, QcBlockFace::Down, QcBlockFace::West};
        const auto it = std::ranges::find(roll, face);
        return it == roll.end() ? face : roll[(static_cast<std::size_t>(it - roll.begin()) + 1) % roll.size()];
    }
    if (turns == turnUp || turns == turnDown || turns == turnAxisZ) {
        // Tilting about the x axis: north, up, south and down follow each other.
        constexpr std::array<QcBlockFace, 4> tilt = {QcBlockFace::North, QcBlockFace::Up, QcBlockFace::South, QcBlockFace::Down};
        const auto it = std::ranges::find(tilt, face);
        const std::size_t step = turns == turnUp ? 1 : 3;
        return it == tilt.end() ? face : tilt[(static_cast<std::size_t>(it - tilt.begin()) + step) % tilt.size()];
    }

    constexpr std::array<QcBlockFace, 4> ring = {QcBlockFace::North, QcBlockFace::East, QcBlockFace::South, QcBlockFace::West};
    for (std::size_t i = 0; i < ring.size(); ++i) {
        if (ring[i] == face) {
            return ring[(i + static_cast<std::size_t>(turns)) % ring.size()];
        }
    }
    return face;
}

// Quarter turns of the texture on the world face `face` of a state turned by `turns`. Horizontal turns
// take the top and bottom textures with them. A lying block (turnAxisX, turnAxisZ) turns the
// textures of the faces along its axis whose upward texture direction (y on the sides, z on the top
// and bottom) crosses the axis, so stripes run along the block. Tilted facings (turnUp, turnDown)
// keep the textures of the sides upright; the top and bottom textures tilt onto the south and north
// sides with their upward direction (north on the top, south on the bottom) turned as the block
// turns, which on the north side is upside down (decision E-874: an observer's arrows keep pointing
// from its face to its back).
quint8 uvTurns(QcBlockFace face, int turns)
{
    const bool vertical = face == QcBlockFace::Up || face == QcBlockFace::Down;
    if (turns == turnUp || turns == turnDown) {
        return face == QcBlockFace::North ? 2 : 0;
    }
    if (turns >= 1 && turns <= 3) {
        if (!vertical) {
            return 0;
        }
        return static_cast<quint8>(face == QcBlockFace::Up ? turns : (4 - turns) % 4);
    }
    if (turns != turnAxisX && turns != turnAxisZ) {
        return 0;
    }

    const QcFaceOffset normal = qcFaceOffset(face);
    const bool alongX = turns == turnAxisX;
    if ((alongX && normal.dx != 0) || (!alongX && normal.dz != 0)) {
        // An end of the block.
        return 0;
    }
    // The texture's upward direction: z on the top and bottom, which crosses an x axis; y on the
    // sides, which crosses both.
    return vertical ? (alongX ? 1 : 0) : 1;
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

// Sixteenths of a turn about the vertical axis that the shape's `spin` property asks for (0..15;
// decision E-766).
int spinSteps(const QcBlockDefinition &block, const QcShapeDefinition &shape, quint32 index)
{
    return shape.spin.isEmpty() ? 0 : std::clamp(QcBlockStateTable::value(block, index, shape.spin).toInt(), 0, 15);
}

// The turns of a state: quarter turns from `rotate` plus the whole quarters of `spin` (only for the
// horizontal turns 0..3; tilts and axes take no spin), and the sixteenths left over.
struct Turns
{
    int quarters = 0;
    int sixteenths = 0;
};

Turns turnsOf(const QcBlockDefinition &block, const QcShapeDefinition &shape, quint32 index)
{
    const int turns = shape.rotate.isEmpty() ? 0 : quarterTurns(QcBlockStateTable::value(block, index, shape.rotate));
    const int steps = spinSteps(block, shape, index);
    if (turns >= turnUp || steps == 0) {
        return {turns, 0};
    }
    return {(turns + steps / 4) % 4, steps % 4};
}

// The texture slot of a part in state `index`: "$<property>" names the slot by the state's value of
// that property (decision E-765), anything else is the slot itself.
QString partTexture(const QcBlockDefinition &block, const QcShapePart &part, quint32 index)
{
    return part.texture.startsWith(u'$') ? QcBlockStateTable::value(block, index, part.texture.mid(1)) : part.texture;
}

// The index of the value of `property` in state `index` (0 for a property the block lacks).
quint64 valueIndex(const QcBlockDefinition &block, quint32 index, const QString &property)
{
    const QcBlockProperty *values = block.property(property);
    return values ? static_cast<quint64>(std::max<qsizetype>(0, values->values.indexOf(QcBlockStateTable::value(block, index, property)))) : 0;
}

// The values of the properties that name texture slots ("$<property>"), 5 bits each from bit 38 on
// (properties have at most 32 values; up to five such properties).
quint64 textureKey(const QcBlockDefinition &block, const QcShapeDefinition &shape, quint32 index)
{
    QStringList seen;
    quint64 key = 0;
    for (const QcShapePart &part : shape.parts) {
        if (part.texture.startsWith(u'$') && !seen.contains(part.texture) && seen.size() < 5) {
            key |= valueIndex(block, index, part.texture.mid(1)) << (38 + 5 * seen.size());
            seen.append(part.texture);
        }
    }
    return key;
}

// Identifies the variant of a state: turns (bits 32-34), flip (bit 35), sixteenths of spin (bits
// 36-37), slots named by properties (bits 38-62) and the parts that apply (bits 0-31). States of one
// block with the same key derive the same data.
quint64 variantKey(const QcBlockDefinition &block, const QcShapeDefinition &shape, quint32 index)
{
    const Turns turns = turnsOf(block, shape, index);
    const bool flip = !shape.flip.isEmpty() && QcBlockStateTable::value(block, index, shape.flip) == u"top"_s;
    quint32 mask = 0;
    for (qsizetype part = 0; part < shape.parts.size() && part < 32; ++part) {
        const bool applies = std::ranges::all_of(shape.parts[part].when, [&](const QcShapeCondition &condition) {
            return condition.values.contains(QcBlockStateTable::value(block, index, condition.property));
        });
        if (applies) {
            mask |= quint32{1} << part;
        }
    }

    return static_cast<quint64>(mask) | (static_cast<quint64>(turns.quarters) << 32) | (static_cast<quint64>(flip ? 1 : 0) << 35)
        | (static_cast<quint64>(turns.sixteenths) << 36)
           | textureKey(block, shape, index);
}

// State `index` with the boxes of the parts that apply to it, turned and flipped as its properties say.
[[nodiscard]] QcBlockState shapedState(const QcBlockDefinition &block, const QcShapeDefinition &shape, quint32 index)
{
    QcBlockState state;
    state.index = index;
    const Turns spun = turnsOf(block, shape, index);
    const int turns = spun.quarters;
    state.fineTurns = static_cast<quint8>(spun.sixteenths);
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
            state.boxTextures.append(partTexture(block, part, index));
            state.boxStretch.append(part.stretch);
        }
        for (const QcAabb &box : part.crosses) {
            state.crosses.append(place(box));
            state.crossTextures.append(partTexture(block, part, index));
        }
        for (const QcAabb &box : part.customCollision ? part.collision : part.boxes) {
            state.collision.append(place(box));
        }
        for (const QcAabb &box : part.target) {
            state.targets.append(place(box));
        }
    }

    for (const QcBlockFace face : qcAllBlockFaces) {
        state.faceSource[static_cast<std::size_t>(turnedFace(face, turns))] = face;
        state.faceUvTurns[static_cast<std::size_t>(face)] = uvTurns(face, turns);
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

    const auto count = static_cast<quint32>(std::max<qsizetype>(1, std::min<qsizetype>(stateCountOf(block), maxStatesPerBlock)));
    QSet<quint64> seen;
    qsizetype boxes = 0;

    for (quint32 index = 0; index < count; ++index) {
        const quint64 key = variantKey(block, shape, index);
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key);

        const quint32 mask = static_cast<quint32>(key);
        for (qsizetype part = 0; part < shape.parts.size(); ++part) {
            if (mask & (quint32{1} << part)) {
                const QcShapePart &applied = shape.parts[part];
                boxes += applied.boxes.size() + applied.crosses.size() + applied.target.size() + (applied.customCollision ? applied.collision.size() : applied.boxes.size());
            }
        }
    }
    return std::max<qsizetype>(1, boxes);
}

bool QcBlockStateTable::fits(std::optional<quint32> type, qsizetype boxes) const
{
    const qsizetype replaced = type && static_cast<qsizetype>(*type) < m_boxes.size() ? m_boxes[static_cast<qsizetype>(*type)] : 0;
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
        const quint64 key = variantKey(block, shape, static_cast<quint32>(index));
        const auto variant = variants.constFind(key);
        if (variant != variants.constEnd()) {
            // Same boxes, faces and flags: share the lists of the first state of this variant.
            QcBlockState state = states[*variant];
            state.index = static_cast<quint32>(index);
            states.append(std::move(state));
            continue;
        }

        variants.insert(key, states.size());
        QcBlockState state = compute(block, shape, static_cast<quint32>(index));
        state.type = type;
        states.append(std::move(state));
    }

    return states;
}

void QcBlockStateTable::append(const QcBlockDefinition &block, const QcShapeDefinition &shape)
{
    const auto type = static_cast<quint32>(m_firstState.size());
    const auto count = static_cast<int>(std::max<qsizetype>(1, stateCountOf(block)));
    m_firstState.append(static_cast<quint32>(m_states.size()));
    m_stateCounts.append(count);

    const qsizetype boxes = derivedBoxes(block, shape);
    m_boxes.append(boxes);
    m_totalBoxes += boxes;

    m_states.append(computeAll(block, shape, count, type));
}

void QcBlockStateTable::update(quint32 type, const QcBlockDefinition &block, const QcShapeDefinition &shape)
{
    const quint32 first = m_firstState[static_cast<qsizetype>(type)];
    const int count = m_stateCounts[static_cast<qsizetype>(type)];

    const qsizetype boxes = derivedBoxes(block, shape);
    m_totalBoxes += boxes - m_boxes[static_cast<qsizetype>(type)];
    m_boxes[static_cast<qsizetype>(type)] = boxes;

    QList<QcBlockState> states = computeAll(block, shape, count, type);
    for (int index = 0; index < count; ++index) {
        m_states[static_cast<qsizetype>(first) + index] = std::move(states[index]);
    }
}

QString QcBlockStateTable::value(const QcBlockDefinition &block, quint32 index, const QString &property)
{
    quint32 rest = index;
    for (const QcBlockProperty &candidate : block.properties) {
        const auto count = static_cast<quint32>(candidate.values.size());
        if (candidate.name == property) {
            return candidate.values[static_cast<qsizetype>(rest % count)];
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
        const auto count = static_cast<quint32>(candidate.values.size());
        if (candidate.name == property) {
            const qsizetype wanted = candidate.values.indexOf(value);
            if (wanted < 0) {
                return std::nullopt;
            }
            const quint32 current = (index / stride) % count;
            return index - current * stride + static_cast<quint32>(wanted) * stride;
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
        const auto count = static_cast<quint32>(property.values.size());
        parts.append(property.name + u'=' + property.values[static_cast<qsizetype>(rest % count)]);
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
        state.fullFace[static_cast<std::size_t>(face)] = coversSide(state.boxes, face);
    }

    // A partial shape of a light-dimming block (stairs, slabs) dims light by its covered faces only.
    if (!state.fullCube && state.lightOpacity > 0) {
        state.lightOpacity = 0;
        for (const QcBlockFace face : qcAllBlockFaces) {
            state.lightBlockedFaces |= state.fullFace[static_cast<std::size_t>(face)] ? QcLightPassage::bit(face) : quint8{0};
        }
    }
    return state;
}
