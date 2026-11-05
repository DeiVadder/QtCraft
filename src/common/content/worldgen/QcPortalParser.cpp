// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/worldgen/QcPortalParser.h"

#include "common/content/registry/QcJsonFieldReader.h"

#include <QJsonArray>

using namespace Qt::StringLiterals;

namespace {

using Definition = QcPortalDefinition;

// A pair [width, height] of "shape" at `key`; `fallback` without the key.
std::pair<int, int> sizeOf(QcJsonFieldReader &reader, const QString &key, std::pair<int, int> fallback)
{
    const QJsonValue value = reader.json.value(key);
    if (value.isUndefined()) {
        return fallback;
    }

    constexpr int largest = Definition::maxInner + 2;
    const QJsonArray pair = value.toArray();
    const int width = pair.size() == 2 ? pair.at(0).toInt(0) : 0;
    const int height = pair.size() == 2 ? pair.at(1).toInt(0) : 0;
    if (!value.isArray() || width < 3 || height < 3 || width > largest || height > largest) {
        reader.fail(u"'%1' must be [width, height], each 3 to %2"_s.arg(key).arg(largest));
        return fallback;
    }
    return {width, height};
}

// Reads one portal: the fields of its JSON into the definition, in parts.
class PortalReading
{
public:
    PortalReading(const QJsonObject &json, QString packNamespace)
        : m_reader{json, u"portal"_s, {}}
        , m_namespace(std::move(packNamespace))
    {
    }

    // The portal, or why it is not one.
    QcResult<Definition> read();

private:
    void readShape();
    void readFrame();
    void readTrigger();
    void readLink();
    void readTimesAndSounds();

    QcJsonFieldReader m_reader;
    QString m_namespace;
    Definition m_portal;
};

// "shape": {"type": vertical|ring, "min": [w, h], "max": [w, h]}.
void PortalReading::readShape()
{
    m_reader.nestedObject(u"shape"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        const QString type = nested.string(u"type"_s, u"vertical"_s);
        if (type == u"ring"_s) {
            m_portal.shape = Definition::Shape::Ring;
        } else if (type != u"vertical"_s) {
            nested.fail(u"'type' must be vertical or ring"_s);
        }
        const auto [minWidth, minHeight] = sizeOf(nested, u"min"_s, {m_portal.minWidth, m_portal.minHeight});
        const auto [maxWidth, maxHeight] = sizeOf(nested, u"max"_s, {m_portal.maxWidth, m_portal.maxHeight});
        m_portal.minWidth = minWidth;
        m_portal.minHeight = minHeight;
        m_portal.maxWidth = maxWidth;
        m_portal.maxHeight = maxHeight;
    });
    if (m_portal.minWidth > m_portal.maxWidth || m_portal.minHeight > m_portal.maxHeight) {
        m_reader.fail(u"'shape' 'min' must not be larger than 'max'"_s);
    }
    if (m_portal.shape == Definition::Shape::Ring && (m_portal.minWidth != m_portal.minHeight || m_portal.maxWidth != m_portal.maxHeight)) {
        m_reader.fail(u"a ring is square: 'min' and 'max' need the same width and height"_s);
    }
}

// "frame": "<block>" or {"block", "insert": {"property", "value"}}.
void PortalReading::readFrame()
{
    if (m_reader.json.value(u"frame"_s).isString()) {
        m_portal.frame = m_reader.name(u"frame"_s, m_namespace);
        return;
    }
    m_reader.nestedObject(u"frame"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        m_portal.frame = nested.name(u"block"_s, m_namespace);
        nested.nestedObject(u"insert"_s, [this](const QJsonObject &, QcJsonFieldReader &insert) {
            m_portal.insertProperty = insert.string(u"property"_s);
            m_portal.insertValue = insert.string(u"value"_s);
            if (m_portal.insertProperty.isEmpty() || m_portal.insertValue.isEmpty()) {
                insert.fail(u"'property' and 'value' are required"_s);
            }
        });
    });
}

// "trigger": {"type": fire|item, "item"}.
void PortalReading::readTrigger()
{
    m_reader.nestedObject(u"trigger"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        const QString type = nested.string(u"type"_s, u"fire"_s);
        if (type == u"item"_s) {
            m_portal.trigger = Definition::Trigger::Item;
            m_portal.triggerItem = nested.name(u"item"_s, m_namespace);
        } else if (type != u"fire"_s) {
            nested.fail(u"'type' must be fire or item"_s);
        }
    });
    if (m_portal.trigger == Definition::Trigger::Item && (m_portal.triggerItem.isEmpty() || m_portal.insertProperty.isEmpty())) {
        m_reader.fail(u"a portal lit by an item needs 'trigger' 'item' and a frame with an 'insert'"_s);
    }
}

// "link": {"type": scaled|fixed, "searchRadius", "createRadius", "platform"}.
void PortalReading::readLink()
{
    m_reader.nestedObject(u"link"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        const QString type = nested.string(u"type"_s, u"scaled"_s);
        if (type == u"fixed"_s) {
            m_portal.link = Definition::Link::Fixed;
        } else if (type != u"scaled"_s) {
            nested.fail(u"'type' must be scaled or fixed"_s);
        }
        m_portal.searchRadius = nested.integer(u"searchRadius"_s, m_portal.searchRadius, 0, 1024);
        m_portal.createRadius = nested.integer(u"createRadius"_s, m_portal.createRadius, 0, 64);
        m_portal.platform = nested.name(u"platform"_s, m_namespace);
    });
    const QString back = m_reader.string(u"back"_s, u"linked"_s);
    if (back == u"spawn"_s) {
        m_portal.back = Definition::Back::Spawn;
    } else if (back != u"linked"_s) {
        m_reader.fail(u"'back' must be linked or spawn"_s);
    }
}

// "wait": {"survival", "creative"}, "cooldown" and "sounds": {"ambient", "travel", "lit"}.
void PortalReading::readTimesAndSounds()
{
    m_reader.nestedObject(u"wait"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        m_portal.survivalWait = nested.number(u"survival"_s, m_portal.survivalWait, 0.0, 60.0);
        m_portal.creativeWait = nested.number(u"creative"_s, m_portal.creativeWait, 0.0, 60.0);
    });
    m_portal.cooldown = m_reader.number(u"cooldown"_s, m_portal.cooldown, 0.0, 600.0);
    m_reader.nestedObject(u"sounds"_s, [this](const QJsonObject &, QcJsonFieldReader &nested) {
        m_portal.ambientSound = nested.name(u"ambient"_s, m_namespace);
        m_portal.travelSound = nested.name(u"travel"_s, m_namespace);
        m_portal.litSound = nested.name(u"lit"_s, m_namespace);
    });
}

QcResult<Definition> PortalReading::read()
{
    m_portal.name = m_reader.name(u"name"_s, m_namespace);
    if (m_portal.name.isEmpty()) {
        return qcError(m_reader.ok() ? u"portal: 'name' is required"_s : m_reader.error);
    }

    m_reader.context = u"portal '%1'"_s.arg(m_portal.name);
    m_portal.from = m_reader.name(u"from"_s, m_namespace, m_portal.from);
    m_portal.to = m_reader.name(u"to"_s, m_namespace);
    m_portal.block = m_reader.name(u"block"_s, m_namespace);
    readFrame();
    readShape();
    readTrigger();
    readLink();
    readTimesAndSounds();
    if (m_reader.ok() && (m_portal.to.isEmpty() || m_portal.block.isEmpty() || m_portal.frame.isEmpty())) {
        m_reader.fail(u"'to', 'block' and 'frame' are required"_s);
    }
    if (m_reader.ok() && m_portal.to == m_portal.from) {
        m_reader.fail(u"'to' must not be 'from'"_s);
    }
    if (!m_reader.ok()) {
        return qcError(m_reader.error);
    }
    return m_portal;
}

} // namespace

namespace QcPortalParser {

QcResult<QcPortalDefinition> parsePortal(const QJsonObject &json, const QString &packNamespace)
{
    return PortalReading(json, packNamespace).read();
}

} // namespace QcPortalParser
