#include "QcShowcaseBlockArea.h"

#include "QcIntMath.h"

#include <algorithm>
#include <array>
#include <tuple>

using namespace Qt::StringLiterals;

namespace {

// Local heights of an area: the ground layer, the podium and the block on show.
constexpr int podiumY = 1;
constexpr int shownY = 2;
constexpr int areaHeight = 6;
// Labels float this far above the top face of what a podium shows, clear of the block itself (a
// label inside an opaque block would be hidden).
constexpr double labelLift = 0.6;

struct Show
{
    quint32 type = 0;
    QcBlockId state = qcAirBlockId;
};

// Where each show goes: line and slot in the line.
struct Slot
{
    int line = 0;
    int column = 0;
};

} // namespace

Q_DECLARE_TYPEINFO(Show, Q_PRIMITIVE_TYPE);
Q_DECLARE_TYPEINFO(Slot, Q_PRIMITIVE_TYPE);

namespace {

int slotsPerLine(qsizetype shows)
{
    if (shows <= 14) {
        return int(std::max<qsizetype>(1, shows));
    }
    return shows <= 100 ? 14 : QcShowcaseBlockArea::maxLineSlots;
}

// A block starts a new line unless it fits into the rest of the current one; longer blocks fill
// whole lines.
QList<Slot> assignSlots(const QList<Show> &shows, int perLine)
{
    QList<Slot> places;
    Slot next;
    for (qsizetype i = 0; i < shows.size(); ++i) {
        const bool firstOfBlock = i == 0 || shows[i - 1].type != shows[i].type;
        if (firstOfBlock && next.column > 0) {
            const qsizetype count = std::ranges::count(shows, shows[i].type, &Show::type);
            if (next.column + count > perLine) {
                next = {next.line + 1, 0};
            }
        }
        if (next.column == perLine) {
            next = {next.line + 1, 0};
        }
        places.append(next);
        ++next.column;
    }
    return places;
}

// The `count` usable coordinates from `start` on in steps of `step` (+1 or -1).
QList<int> usableCoordinates(int start, int step, int count, bool (*usable)(int))
{
    QList<int> result;
    for (int value = start; result.size() < count; value += step) {
        if (usable(value)) {
            result.append(value);
        }
    }
    return result;
}

// "[facing=north,half=top]" as "facing=north, half=top".
QString stateDetail(const QcContentRegistry &content, QcBlockId state)
{
    const QcBlockState &info = content.state(state);
    QString text = QcBlockStateTable::describe(content.block(state), info.index);
    if (text.size() >= 2) {
        text = text.mid(1, text.size() - 2).replace(u","_s, u", "_s);
    }
    return text;
}

QcBlockId podiumFor(const QcShowcaseContext &context, const QcBlockDefinition &block)
{
    // Fire stands on a block with eternal fire, so it burns on its podium for ever (decision E-392).
    if (block.fire.isFire) {
        for (const QcBlockDefinition &candidate : context.content.blocks()) {
            if (const std::optional<QcBlockId> id = context.content.blockId(candidate.name); id && candidate.fire.eternal) {
                return *id;
            }
        }
    }
    for (const QString &ground : block.groundBlocks) {
        if (const std::optional<QcBlockId> id = context.content.blockId(ground)) {
            return *id;
        }
    }
    return context.podium;
}

// Puts one state on its podium at local (x, z); returns the height of its label.
double placeShow(QcStructure &structure, const QcShowcaseContext &context, QcBlockId state, int x, int z)
{
    const QcContentRegistry &content = context.content;
    const QcBlockDefinition &block = content.block(state);
    structure.set(x, podiumY, z, podiumFor(context, block));
    structure.set(x, shownY, z, state);
    int top = shownY;

    const QcBlockProperty *half = block.property(u"half"_s);
    if (block.door && half && half->values.size() >= 2) {
        structure.set(x, shownY + 1, z, content.withProperty(state, u"half"_s, half->values[1]));
        top = shownY + 1;
    }

    const QString attach = content.property(state, u"attach"_s);
    const QString facing = content.property(state, u"facing"_s);
    const std::array<std::tuple<QString, int, int>, 4> sides{
        {{u"north"_s, 0, -1}, {u"east"_s, 1, 0}, {u"south"_s, 0, 1}, {u"west"_s, -1, 0}}};
    for (const auto &[side, dx, dz] : sides) {
        // A bed's head lies beside its foot in the facing direction, on a podium of its own.
        if (block.bed && facing == side) {
            structure.set(x + dx, podiumY, z + dz, context.podium);
            structure.set(x + dx, shownY, z + dz, content.withProperty(state, u"part"_s, u"head"_s));
        }
        if (attach == side) {
            structure.set(x + dx, shownY, z + dz, context.podium);
        }
        if (block.liquid) {
            structure.set(x + dx, shownY, z + dz, context.enclosure);
        }
    }
    if (attach == u"ceiling"_s) {
        structure.set(x, shownY + 1, z, context.podium);
        top = shownY + 1;
    }
    return top + 1 + labelLift;
}

QList<Show> showsOf(const QcContentRegistry &content, const QList<quint32> &types)
{
    QList<Show> shows;
    for (const quint32 type : types) {
        const QList<QcBlockId> states = QcShowcaseBlockArea::shownStates(content, type);
        for (const QcBlockId state : states) {
            shows.append({type, state});
        }
    }
    return shows;
}

} // namespace

namespace QcShowcaseBlockArea {

bool usableX(int x)
{
    const int local = qcFloorMod(x, QcWorld::chunkSize);
    return local % 2 == 1 && local <= QcWorld::chunkSize - 3;
}

bool usableZ(int z)
{
    const int local = qcFloorMod(z, QcWorld::chunkSize);
    return local % 3 == 1 && local <= QcWorld::chunkSize - 3;
}

QList<QcBlockId> shownStates(const QcContentRegistry &content, quint32 type)
{
    const QcBlockDefinition &block = content.blocks()[type];
    const QcBlockStateTable &table = content.stateTable();
    const QcBlockProperty *half = block.property(u"half"_s);
    const QcBlockId first = table.firstState(type);
    // Blocks with very many states (Signal Dust: 16 strengths times 81 shapes, the repeater) show
    // the default state and every state that differs from it in one property only (decision E-340).
    const bool axesOnly = table.stateCount(type) > maxAllStates;
    QList<QcBlockId> states;
    for (int index = 0; index < table.stateCount(type); ++index) {
        const QcBlockId state = first + quint32(index);
        if (block.door && half && content.property(state, u"half"_s) != half->values.value(0)) {
            continue;
        }
        // A bed shows its foot with the head beside it, like a door its lower half with the upper.
        if (block.bed && content.property(state, u"part"_s) == u"head"_s) {
            continue;
        }
        const auto differs = [&](const QcBlockProperty &property) { return content.property(state, property.name) != content.property(first, property.name); };
        if (axesOnly && std::ranges::count_if(block.properties, differs) > 1) {
            continue;
        }
        states.append(state);
    }
    return states;
}

QcShowcaseArea build(const QcShowcaseContext &context, const QcShowcaseCategory &category, const QList<quint32> &types,
                     QcBlockPos anchor)
{
    const QList<Show> shows = showsOf(context.content, types);
    const QList<Slot> places = assignSlots(shows, slotsPerLine(shows.size()));
    const int lines = places.isEmpty() ? 1 : places.constLast().line + 1;
    const int columns = std::max(1, slotsPerLine(shows.size()));

    // Lines from the south edge northwards, podiums from the west edge eastwards; one block of room
    // on every side for what hangs next to a podium.
    const QList<int> xs = usableCoordinates(anchor.x + 1, 1, columns, usableX);
    const QList<int> zs = usableCoordinates(anchor.z - 1, -1, lines, usableZ);
    const QcBlockPos origin{anchor.x, anchor.y, zs.constLast() - 2};
    auto structure = std::make_shared<QcStructure>(u"showcase/"_s + category.name, xs.constLast() - anchor.x + 2, areaHeight,
                                                   anchor.z - origin.z + 1);

    for (qsizetype i = 0; i < shows.size(); ++i) {
        const int x = xs[places[i].column] - origin.x;
        const int z = zs[places[i].line] - origin.z;
        const double labelY = placeShow(*structure, context, shows[i].state, x, z);
        const QcBlockDefinition &block = context.content.block(shows[i].state);
        structure->labels.append({{x + 0.5, labelY, z + 0.5}, block.translationKey, stateDetail(context.content, shows[i].state), false});
    }
    return {u"blocks-"_s + category.name, category.translationKey, std::move(structure), origin, {}};
}

} // namespace QcShowcaseBlockArea
