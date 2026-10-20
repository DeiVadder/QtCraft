#include "server/signal/QcSignalTable.h"

#include "common/content/registry/QcContentRegistry.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

[[nodiscard]] std::optional<QcBlockFace> faceNamed(const QString &name)
{
    static const std::array<std::pair<QString, QcBlockFace>, 8> faces{{
        {u"north"_s, QcBlockFace::North},
        {u"east"_s, QcBlockFace::East},
        {u"south"_s, QcBlockFace::South},
        {u"west"_s, QcBlockFace::West},
        {u"up"_s, QcBlockFace::Up},
        {u"down"_s, QcBlockFace::Down},
        {u"floor"_s, QcBlockFace::Down},
        {u"ceiling"_s, QcBlockFace::Up},
    }};
    for (const auto &[text, face] : faces) {
        if (text == name) {
            return face;
        }
    }
    return std::nullopt;
}

// The dust properties of the sides, in the order of QcSignalTable::horizontal.
const std::array<QString, 4> &sideNames()
{
    static const std::array<QString, 4> names{u"north"_s, u"east"_s, u"south"_s, u"west"_s};
    return names;
}

QcDustSide sideNamed(const QString &name)
{
    if (name == u"side"_s) {
        return QcDustSide::Side;
    }
    return name == u"up"_s ? QcDustSide::Up : QcDustSide::None;
}

QString sideName(QcDustSide side)
{
    switch (side) {
    case QcDustSide::Side:
        return u"side"_s;
    case QcDustSide::Up:
        return u"up"_s;
    case QcDustSide::None:
        break;
    }
    return u"none"_s;
}

// Without a choice in data: unbreakable blocks stay, blocks that hang on others, wash away, flow or
// give way break, everything else (chests and furnaces too) moves.
QcPistonMobility mobilityOf(const QcBlockDefinition &block)
{
    if (block.signal.mobility != QcPistonMobility::Default) {
        return block.signal.mobility;
    }
    if (block.hardness < 0.0) {
        return QcPistonMobility::Immovable;
    }
    if (block.liquid || block.replaceable || block.washable || block.door || block.support != QcBlockSupport::None) {
        return QcPistonMobility::Breaks;
    }
    return QcPistonMobility::Push;
}

// Sources whose strength comes from their definition while they are on.
bool emitsWhileOn(QcSignalKind kind)
{
    switch (kind) {
    case QcSignalKind::Lever:
    case QcSignalKind::Button:
    case QcSignalKind::Plate:
    case QcSignalKind::Torch:
    case QcSignalKind::Repeater:
    case QcSignalKind::Observer:
    case QcSignalKind::Constant:
    case QcSignalKind::Hook:
        return true;
    default:
        return false;
    }
}

bool isFlag(const QcContentRegistry &content, QcBlockId id, const QString &property)
{
    return content.property(id, property) == u"true"_s;
}

} // namespace

int QcSignalTable::horizontalIndex(QcBlockFace face)
{
    const auto it = std::ranges::find(horizontal, face);
    return it == horizontal.end() ? -1 : int(it - horizontal.begin());
}

QcSignalTable::QcSignalTable(const QcContentRegistry &content)
    : m_content(content)
{
    const auto count = std::size_t(content.stateCount());
    m_states.reserve(count);
    for (std::size_t id = 0; id < count; ++id) {
        m_states.push_back(decode(content, QcBlockId(id)));
    }

    for (qsizetype type = 0; type < content.blocks().size(); ++type) {
        if (content.blocks()[type].signal.kind == QcSignalKind::Dust) {
            if (const std::optional<DustLayout> layout = dustLayout(content, quint32(type))) {
                m_dust.insert(quint32(type), *layout);
            }
        }
    }
}

bool QcSignalTable::DustLayout::place(const QcBlockProperty &property, int stride)
{
    if (property.name == u"power"_s) {
        powerStride = stride;
        for (int power = 0; power <= QcSignalDefinition::maxPower; ++power) {
            powerValue[std::size_t(power)] = int(property.values.indexOf(QString::number(power)));
        }
        return true;
    }

    const auto side = std::ranges::find(sideNames(), property.name);
    if (side == sideNames().end()) {
        return false;
    }
    const auto index = std::size_t(side - sideNames().begin());
    sideStride[index] = stride;
    for (const QcDustSide value : {QcDustSide::None, QcDustSide::Side, QcDustSide::Up}) {
        sideValue[index][std::size_t(value)] = int(property.values.indexOf(sideName(value)));
    }
    return true;
}

std::optional<QcSignalTable::DustLayout> QcSignalTable::dustLayout(const QcContentRegistry &content, quint32 type)
{
    const QcBlockDefinition &block = content.blocks()[qsizetype(type)];
    DustLayout layout;
    layout.first = content.stateTable().firstState(type);
    int stride = 1;
    int found = 0;

    for (const QcBlockProperty &property : block.properties) {
        found += layout.place(property, stride) ? 1 : 0;
        stride *= int(property.values.size());
    }

    // The parser made sure every value exists (QcSignalParser).
    return found == 5 ? std::optional<DustLayout>(layout) : std::nullopt;
}

QcSignalState QcSignalTable::decode(const QcContentRegistry &content, QcBlockId id)
{
    QcSignalState state;
    if (!content.isValidBlock(id)) {
        return state;
    }

    const QcBlockDefinition &block = content.block(id);
    const QcBlockState &shape = content.state(id);
    const QcSignalDefinition &signal = block.signal;
    state.kind = signal.kind;
    // A constant source and a target power only components and dust, never the blocks around them
    // (decisions E-752, E-768).
    const bool source = signal.kind == QcSignalKind::Constant || signal.kind == QcSignalKind::Target;
    state.conductor = signal.conducts && !source && block.solid && shape.opaque && shape.fullCube;
    state.mobility = mobilityOf(block);

    const bool flag = isFlag(content, id, u"powered"_s) || isFlag(content, id, u"extended"_s) || isFlag(content, id, u"open"_s)
                      || isFlag(content, id, u"sticky"_s) || isFlag(content, id, u"triggered"_s);
    state.on = signal.kind == QcSignalKind::Torch || signal.kind == QcSignalKind::Lamp ? signal.active : (flag || signal.kind == QcSignalKind::Constant);
    state.facing = faceNamed(content.property(id, block.property(u"attach"_s) ? u"attach"_s : u"facing"_s));
    state.delay = quint8(std::clamp(content.property(id, u"delay"_s).toInt(), 1, 4));
    state.locked = isFlag(content, id, u"locked"_s) || content.property(id, u"enabled"_s) == u"false"_s;
    state.subtract = content.property(id, u"mode"_s) == u"subtract"_s;
    state.strongPowersAbove = signal.strongPowersAbove;

    const bool counted = signal.kind == QcSignalKind::Target || signal.kind == QcSignalKind::Viewers;
    if (signal.kind == QcSignalKind::Dust || signal.kind == QcSignalKind::Comparator || counted) {
        state.power = quint8(std::clamp(content.property(id, u"power"_s).toInt(), 0, QcSignalDefinition::maxPower));
    }
    if (counted) {
        state.on = state.power > 0;
    }
    if (signal.kind == QcSignalKind::Dust) {
        for (std::size_t i = 0; i < horizontal.size(); ++i) {
            state.sides[i] = sideNamed(content.property(id, sideNames()[i]));
        }
    } else if (state.on && emitsWhileOn(signal.kind)) {
        state.power = quint8(signal.power);
    }
    return state;
}

QcBlockId QcSignalTable::dustState(QcBlockId dust, int power, std::array<QcDustSide, 4> sides) const
{
    const auto layout = m_dust.constFind(m_content.state(dust).type);
    if (layout != m_dust.constEnd()) {
        int index = layout->powerValue[std::size_t(std::clamp(power, 0, QcSignalDefinition::maxPower))] * layout->powerStride;
        for (std::size_t i = 0; i < sides.size(); ++i) {
            index += layout->sideValue[i][std::size_t(sides[i])] * layout->sideStride[i];
        }
        return layout->first + QcBlockId(index);
    }

    QcBlockId id = m_content.withProperty(dust, u"power"_s, QString::number(std::clamp(power, 0, QcSignalDefinition::maxPower)));
    for (std::size_t i = 0; i < sideNames().size(); ++i) {
        id = m_content.withProperty(id, sideNames()[i], sideName(sides[i]));
    }
    return id;
}

QcBlockId QcSignalTable::withFlag(QcBlockId id, const QString &property, bool value) const
{
    return m_content.withProperty(id, property, value ? u"true"_s : u"false"_s);
}

QcBlockId QcSignalTable::counterpartOf(QcBlockId id) const
{
    const QcBlockDefinition &block = m_content.block(id);
    const std::optional<QcBlockId> other = m_content.blockId(block.signal.counterpart);
    if (!other) {
        return id;
    }

    // Same property values in the other block.
    QcBlockId result = *other;
    for (const QcBlockProperty &property : block.properties) {
        result = m_content.withProperty(result, property.name, m_content.property(id, property.name));
    }
    return result;
}
