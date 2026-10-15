#include "QcSignalParser.h"

#include "QcJsonFieldReader.h"

#include <array>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

constexpr std::array<std::pair<QcSignalKind, const char *>, qcSignalKindCount - 1> kindNames{{
    {QcSignalKind::Dust, "dust"},
    {QcSignalKind::Lever, "lever"},
    {QcSignalKind::Button, "button"},
    {QcSignalKind::Plate, "plate"},
    {QcSignalKind::Torch, "torch"},
    {QcSignalKind::Repeater, "repeater"},
    {QcSignalKind::Lamp, "lamp"},
    {QcSignalKind::Opener, "opener"},
    {QcSignalKind::Piston, "piston"},
    {QcSignalKind::PistonHead, "piston_head"},
    {QcSignalKind::Comparator, "comparator"},
    {QcSignalKind::Observer, "observer"},
    {QcSignalKind::Hopper, "hopper"},
    {QcSignalKind::Dispenser, "dispenser"},
    {QcSignalKind::Rail, "rail"},
}};

[[nodiscard]] std::optional<QcSignalKind> kindFromName(const QString &name)
{
    for (const auto &[kind, text] : kindNames) {
        if (name == QLatin1StringView(text)) {
            return kind;
        }
    }
    return std::nullopt;
}

// Whether the block has `property` with every one of `values`.
bool hasValues(const QcBlockDefinition &block, const QString &property, const QStringList &values)
{
    const QcBlockProperty *found = block.property(property);
    if (!found) {
        return false;
    }
    for (const QString &value : values) {
        if (!found->values.contains(value)) {
            return false;
        }
    }
    return true;
}

const QStringList booleans{u"false"_s, u"true"_s};
const QStringList horizontal{u"north"_s, u"east"_s, u"south"_s, u"west"_s};
const QStringList allSides{u"north"_s, u"east"_s, u"south"_s, u"west"_s, u"up"_s, u"down"_s};

QStringList powerValues()
{
    QStringList values;
    for (int power = 0; power <= QcSignalDefinition::maxPower; ++power) {
        values.append(QString::number(power));
    }
    return values;
}

// The state properties every block of a kind needs, as property name and the values it must allow.
QList<std::pair<QString, QStringList>> requiredStates(QcSignalKind kind)
{
    switch (kind) {
    case QcSignalKind::Dust:
        return {{u"power"_s, powerValues()},
                {u"north"_s, {u"none"_s, u"side"_s, u"up"_s}},
                {u"east"_s, {u"none"_s, u"side"_s, u"up"_s}},
                {u"south"_s, {u"none"_s, u"side"_s, u"up"_s}},
                {u"west"_s, {u"none"_s, u"side"_s, u"up"_s}}};
    case QcSignalKind::Lever:
    case QcSignalKind::Button:
    case QcSignalKind::Plate:
        return {{u"powered"_s, booleans}};
    case QcSignalKind::Torch:
        return {{u"attach"_s, {u"floor"_s}}};
    case QcSignalKind::Repeater:
        return {{u"facing"_s, horizontal}, {u"delay"_s, {u"1"_s, u"2"_s, u"3"_s, u"4"_s}}, {u"powered"_s, booleans}, {u"locked"_s, booleans}};
    case QcSignalKind::Opener:
        return {{u"open"_s, booleans}};
    case QcSignalKind::Piston:
        return {{u"facing"_s, allSides}, {u"extended"_s, booleans}};
    case QcSignalKind::PistonHead:
        return {{u"facing"_s, allSides}, {u"sticky"_s, booleans}};
    case QcSignalKind::Comparator:
        return {{u"facing"_s, horizontal}, {u"mode"_s, {u"compare"_s, u"subtract"_s}}, {u"powered"_s, booleans}, {u"power"_s, powerValues()}};
    case QcSignalKind::Observer:
        return {{u"facing"_s, allSides}, {u"powered"_s, booleans}};
    case QcSignalKind::Hopper:
        return {{u"facing"_s, {u"down"_s, u"north"_s, u"east"_s, u"south"_s, u"west"_s}}, {u"enabled"_s, booleans}};
    case QcSignalKind::Dispenser:
        return {{u"facing"_s, allSides}, {u"triggered"_s, booleans}};
    case QcSignalKind::Rail:
        return {{u"powered"_s, booleans}};
    case QcSignalKind::Lamp:
    case QcSignalKind::None:
        return {};
    }
    return {};
}

void parseMobility(QcSignalDefinition &signal, QcJsonFieldReader &reader)
{
    const QString movable = reader.string(u"movable"_s, u"default"_s);
    if (movable == u"push"_s) {
        signal.mobility = QcPistonMobility::Push;
    } else if (movable == u"immovable"_s) {
        signal.mobility = QcPistonMobility::Immovable;
    } else if (movable == u"breaks"_s) {
        signal.mobility = QcPistonMobility::Breaks;
    } else if (movable != u"default"_s) {
        reader.fail(u"'movable' must be default, push, immovable or breaks"_s);
    }
}

void parseKindFields(QcSignalDefinition &signal, QcJsonFieldReader &reader, const QString &packNamespace)
{
    signal.power = reader.integer(u"power"_s, QcSignalDefinition::maxPower, 1, QcSignalDefinition::maxPower);
    signal.pulseTicks = reader.integer(u"pulseTicks"_s, 20, 1, QcSignalDefinition::maxPulseTicks);
    signal.offDelayTicks = reader.integer(u"offDelayTicks"_s, 4, 0, QcSignalDefinition::maxPulseTicks);

    const QString detects = reader.string(u"detects"_s, u"all"_s);
    if (detects != u"all"_s && detects != u"living"_s && detects != u"carts"_s) {
        reader.fail(u"'detects' must be all, living or carts"_s);
    }
    signal.detectsItems = detects == u"all"_s;
    signal.detectsCarts = detects == u"carts"_s;
    signal.railReach = reader.integer(u"reach"_s, 8, 0, QcSignalDefinition::maxRailReach);

    signal.sticky = reader.boolean(u"sticky"_s, false);
    signal.pushLimit = reader.integer(u"pushLimit"_s, 12, 1, QcSignalDefinition::maxPushLimit);
    signal.quasiConnectivity = reader.boolean(u"quasiConnectivity"_s, true);
    signal.active = reader.boolean(u"active"_s, true);
    signal.counterpart = reader.name(u"counterpart"_s, packNamespace);
    signal.head = reader.name(u"head"_s, packNamespace);
    signal.usesItems = reader.boolean(u"uses"_s, true);
}

// "comparator": "contents" or {"property": "<name>"} (decision E-343).
void parseReading(QcBlockDefinition &block, QcJsonFieldReader &reader)
{
    const QJsonValue value = reader.json.value(u"comparator"_s);
    QcSignalDefinition &signal = block.signal;
    if (value.isUndefined()) {
        return;
    }
    if (value.isString() && value.toString() == u"carts"_s) {
        if (!block.rail) {
            reader.fail(u"'comparator': \"carts\" needs a rail"_s);
            return;
        }
        signal.reading = QcComparatorReading::Carts;
        return;
    }
    if (value.isString() && value.toString() == u"contents"_s) {
        if (block.containerSlots == 0 && !block.machine.isMachine()) {
            reader.fail(u"'comparator': \"contents\" needs a container or a machine"_s);
            return;
        }
        signal.reading = QcComparatorReading::Contents;
        return;
    }
    if (value.isObject()) {
        const QString property = value.toObject().value(u"property"_s).toString();
        const QcBlockProperty *found = block.property(property);
        if (!found || found->values.size() < 2) {
            reader.fail(u"'comparator' names '%1', which is no state property with at least two values"_s.arg(property));
            return;
        }
        signal.reading = QcComparatorReading::Property;
        signal.readProperty = property;
        return;
    }
    reader.fail(u"'comparator' must be \"contents\", \"carts\" or an object with 'property'"_s);
}

void checkKind(const QcBlockDefinition &block, QcJsonFieldReader &reader)
{
    const QcSignalDefinition &signal = block.signal;
    const QList<std::pair<QString, QStringList>> required = requiredStates(signal.kind);
    for (const auto &[property, values] : required) {
        if (reader.ok() && !hasValues(block, property, values)) {
            reader.fail(u"a %1 needs the state '%2' with the values %3"_s.arg(QcSignalParser::kindName(signal.kind), property,
                                                                               values.join(u", "_s)));
        }
    }

    if (reader.ok() && signal.kind == QcSignalKind::Piston && signal.head.isEmpty()) {
        reader.fail(u"a piston needs 'head', the block of its extended head"_s);
    }
    if (reader.ok() && (signal.kind == QcSignalKind::Torch || signal.kind == QcSignalKind::Lamp) && signal.counterpart.isEmpty()) {
        reader.fail(u"torches and lamps need 'counterpart', the block of the other light state"_s);
    }
}

} // namespace

namespace QcSignalParser {

QString kindName(QcSignalKind kind)
{
    for (const auto &[candidate, text] : kindNames) {
        if (candidate == kind) {
            return QString::fromLatin1(text);
        }
    }
    return {};
}

void parseSignal(QcBlockDefinition &block, QcJsonFieldReader &signalReader, const QString &packNamespace)
{
    QcSignalDefinition &signal = block.signal;
    const QString type = signalReader.string(u"type"_s);
    if (!type.isEmpty()) {
        const std::optional<QcSignalKind> kind = kindFromName(type);
        if (!kind) {
            signalReader.fail(u"unknown signal type '%1'"_s.arg(type));
            return;
        }
        signal.kind = *kind;
    }

    parseKindFields(signal, signalReader, packNamespace);
    parseMobility(signal, signalReader);
    parseReading(block, signalReader);
    // Pistons and observers pass no power through (as in the reference).
    const bool conducts = signal.kind != QcSignalKind::Piston && signal.kind != QcSignalKind::PistonHead && signal.kind != QcSignalKind::Observer;
    signal.conducts = signalReader.boolean(u"conducts"_s, conducts);
    checkKind(block, signalReader);
}

} // namespace QcSignalParser
