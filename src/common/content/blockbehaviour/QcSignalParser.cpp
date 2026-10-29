// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/blockbehaviour/QcSignalParser.h"

#include "common/content/registry/QcContentNames.h"
#include "common/content/registry/QcJsonFieldReader.h"

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
    {QcSignalKind::Igniter, "igniter"},
    {QcSignalKind::Constant, "constant"},
    {QcSignalKind::Note, "note"},
    {QcSignalKind::Target, "target"},
    {QcSignalKind::Hook, "hook"},
    {QcSignalKind::Tripwire, "tripwire"},
    {QcSignalKind::Viewers, "viewers"},
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

QStringList noteValues()
{
    QStringList values;
    for (int note = 0; note < QcSignalDefinition::noteCount; ++note) {
        values.append(QString::number(note));
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
    case QcSignalKind::Note:
        return {{u"powered"_s, booleans}, {u"note"_s, noteValues()}};
    case QcSignalKind::Target:
    case QcSignalKind::Viewers:
        return {{u"power"_s, powerValues()}};
    case QcSignalKind::Hook:
        return {{u"attach"_s, horizontal}, {u"attached"_s, booleans}, {u"powered"_s, booleans}};
    case QcSignalKind::Tripwire:
        return {{u"attached"_s, booleans}, {u"powered"_s, booleans}, {u"disarmed"_s, booleans}};
    case QcSignalKind::Lamp:
    case QcSignalKind::Igniter:
    case QcSignalKind::Constant:
    case QcSignalKind::None:
        return {};
    }
    return {};
}

// Reads the "signal" object of one block: owns the signal definition being built; `block` is the block
// read so far, whose state properties the kind needs.
class SignalReader
{
public:
    SignalReader(const QcBlockDefinition &block, QcJsonFieldReader &reader, const QString &packNamespace)
        : m_block(block)
        , m_reader(reader)
        , m_namespace(packNamespace)
    {
    }

    [[nodiscard]] QcSignalDefinition read();

private:
    void readMobility();
    void readKindFields();
    void readReading();
    void readInstruments(const QJsonObject &json, QcJsonFieldReader &reader);
    void checkKind() const;

    const QcBlockDefinition &m_block;
    QcJsonFieldReader &m_reader;
    const QString &m_namespace;
    QcSignalDefinition m_signal;
};

void SignalReader::readMobility()
{
    const QString movable = m_reader.string(u"movable"_s, u"default"_s);
    if (movable == u"push"_s) {
        m_signal.mobility = QcPistonMobility::Push;
    } else if (movable == u"immovable"_s) {
        m_signal.mobility = QcPistonMobility::Immovable;
    } else if (movable == u"breaks"_s) {
        m_signal.mobility = QcPistonMobility::Breaks;
    } else if (movable != u"default"_s) {
        m_reader.fail(u"'movable' must be default, push, immovable or breaks"_s);
    }
}

void SignalReader::readKindFields()
{
    m_signal.power = m_reader.integer(u"power"_s, QcSignalDefinition::maxPower, 1, QcSignalDefinition::maxPower);
    m_signal.pulseTicks = m_reader.integer(u"pulseTicks"_s, 20, 1, QcSignalDefinition::maxPulseTicks);
    m_signal.offDelayTicks = m_reader.integer(u"offDelayTicks"_s, 4, 0, QcSignalDefinition::maxPulseTicks);

    const QString detects = m_reader.string(u"detects"_s, u"all"_s);
    if (detects != u"all"_s && detects != u"living"_s && detects != u"carts"_s) {
        m_reader.fail(u"'detects' must be all, living or carts"_s);
    }
    m_signal.detectsItems = detects == u"all"_s;
    m_signal.detectsCarts = detects == u"carts"_s;
    m_signal.railReach = m_reader.integer(u"reach"_s, 8, 0, QcSignalDefinition::maxRailReach);
    m_signal.wireReach = m_reader.integer(u"wireReach"_s, 39, 1, QcSignalDefinition::maxWireReach);

    m_signal.sticky = m_reader.boolean(u"sticky"_s, false);
    m_signal.pushLimit = m_reader.integer(u"pushLimit"_s, 12, 1, QcSignalDefinition::maxPushLimit);
    m_signal.quasiConnectivity = m_reader.boolean(u"quasiConnectivity"_s, true);
    m_signal.active = m_reader.boolean(u"active"_s, true);
    m_signal.counterpart = m_reader.name(u"counterpart"_s, m_namespace);
    m_signal.strongPowersAbove = m_reader.boolean(u"strongPowersAbove"_s, true);
    m_signal.head = m_reader.name(u"head"_s, m_namespace);
    m_signal.usesItems = m_reader.boolean(u"uses"_s, true);
    m_signal.stuckHitTicks = m_reader.integer(u"stuckHitTicks"_s, 20, 1, QcSignalDefinition::maxPulseTicks);
    m_signal.hitTicks = m_reader.integer(u"hitTicks"_s, 8, 1, QcSignalDefinition::maxPulseTicks);
    m_reader.nestedObject(u"instruments"_s, [this](const QJsonObject &object, QcJsonFieldReader &nested) { readInstruments(object, nested); });
}

// "instruments": {"default": "note.harp", "blocks": {"gold_block": "note.bell"}, "sounds": {"wood": "note.bass"}}
// (decision E-767); names and sounds without a namespace get the pack's.
void SignalReader::readInstruments(const QJsonObject &json, QcJsonFieldReader &reader)
{
    m_signal.instruments.fallback = reader.name(u"default"_s, m_namespace);
    const auto readMap = [this, &json, &reader](const QString &key) {
        QHash<QString, QString> map;
        const QJsonObject entries = json.value(key).toObject();
        for (auto it = entries.begin(); it != entries.end(); ++it) {
            if (!it.value().isString() || map.size() >= QcSignalDefinition::maxInstruments) {
                reader.fail(u"'%1' maps names to sounds (at most %2)"_s.arg(key).arg(QcSignalDefinition::maxInstruments));
                break;
            }
            map.insert(QcContentNames::qualify(it.key(), m_namespace), QcContentNames::qualify(it.value().toString(), m_namespace));
        }
        return map;
    };
    m_signal.instruments.byBlock = readMap(u"blocks"_s);
    m_signal.instruments.bySound = readMap(u"sounds"_s);
}

// "comparator": "contents" or {"property": "<name>"} (decision E-343).
void SignalReader::readReading()
{
    const QJsonValue value = m_reader.json.value(u"comparator"_s);
    if (value.isUndefined()) {
        return;
    }
    if (value.isString() && value.toString() == u"carts"_s) {
        if (!m_block.rail) {
            m_reader.fail(u"'comparator': \"carts\" needs a rail"_s);
            return;
        }
        m_signal.reading = QcComparatorReading::Carts;
        return;
    }
    if (value.isString() && value.toString() == u"contents"_s) {
        if (m_block.containerSlots == 0 && !m_block.machine.isMachine()) {
            m_reader.fail(u"'comparator': \"contents\" needs a container or a machine"_s);
            return;
        }
        m_signal.reading = QcComparatorReading::Contents;
        return;
    }
    if (value.isString() && value.toString() == u"bites"_s) {
        // The block's bites are read before its signal; their parser checks the state 'bites'.
        if (!m_block.bites.isEdible()) {
            m_reader.fail(u"'comparator': \"bites\" needs 'bites'"_s);
            return;
        }
        m_signal.reading = QcComparatorReading::Bites;
        return;
    }
    if (value.isObject()) {
        const QString property = value.toObject().value(u"property"_s).toString();
        const QcBlockProperty *found = m_block.property(property);
        if (!found || found->values.size() < 2) {
            m_reader.fail(u"'comparator' names '%1', which is no state property with at least two values"_s.arg(property));
            return;
        }
        m_signal.reading = QcComparatorReading::Property;
        m_signal.readProperty = property;
        return;
    }
    m_reader.fail(u"'comparator' must be \"contents\", \"carts\", \"bites\" or an object with 'property'"_s);
}

void SignalReader::checkKind() const
{
    const QList<std::pair<QString, QStringList>> required = requiredStates(m_signal.kind);
    for (const auto &[property, values] : required) {
        if (m_reader.ok() && !hasValues(m_block, property, values)) {
            m_reader.fail(u"a %1 needs the state '%2' with the values %3"_s.arg(QcSignalParser::kindName(m_signal.kind), property,
                                                                                 values.join(u", "_s)));
        }
    }

    if (m_reader.ok() && m_signal.kind == QcSignalKind::Piston && m_signal.head.isEmpty()) {
        m_reader.fail(u"a piston needs 'head', the block of its extended head"_s);
    }
    if (m_reader.ok() && (m_signal.kind == QcSignalKind::Torch || m_signal.kind == QcSignalKind::Lamp) && m_signal.counterpart.isEmpty()) {
        m_reader.fail(u"torches and lamps need 'counterpart', the block of the other light state"_s);
    }
}

QcSignalDefinition SignalReader::read()
{
    const QString type = m_reader.string(u"type"_s);
    if (!type.isEmpty()) {
        const std::optional<QcSignalKind> kind = kindFromName(type);
        if (!kind) {
            m_reader.fail(u"unknown signal type '%1'"_s.arg(type));
            return m_signal;
        }
        m_signal.kind = *kind;
    }

    readKindFields();
    readMobility();
    readReading();
    // Pistons and observers pass no power through (as in the reference).
    const bool conducts = m_signal.kind != QcSignalKind::Piston && m_signal.kind != QcSignalKind::PistonHead
                          && m_signal.kind != QcSignalKind::Observer;
    m_signal.conducts = m_reader.boolean(u"conducts"_s, conducts);
    checkKind();
    return m_signal;
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

QcSignalDefinition parseSignal(const QcBlockDefinition &block, QcJsonFieldReader &signalReader, const QString &packNamespace)
{
    return SignalReader(block, signalReader, packNamespace).read();
}

} // namespace QcSignalParser
