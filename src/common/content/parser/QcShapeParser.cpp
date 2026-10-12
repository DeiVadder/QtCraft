#include "QcShapeParser.h"

#include "QcBlockProperty.h"
#include "QcContentFields.h"
#include "QcJsonFieldReader.h"

#include <QJsonArray>

#include <algorithm>
#include <array>

using namespace Qt::StringLiterals;

namespace {

// Boxes are six numbers [x0, y0, z0, x1, y1, z1] in block-local metres.
QList<QcAabb> parseShapeBoxes(const QJsonValue &value, double maxY, const QString &key, QcJsonFieldReader &reader)
{
    QList<QcAabb> boxes;
    if (value.isUndefined()) {
        return boxes;
    }
    const QJsonArray list = value.toArray();
    if (!value.isArray() || list.size() > QcShapeDefinition::maxBoxesPerPart) {
        reader.fail(u"'%1' must be an array of at most %2 boxes"_s.arg(key).arg(QcShapeDefinition::maxBoxesPerPart));
        return boxes;
    }

    for (const auto &entry : list) {
        const QJsonArray numbers = entry.toArray();
        bool ok = entry.isArray() && numbers.size() == 6;
        std::array<double, 6> v{};
        for (int i = 0; ok && i < 6; ++i) {
            ok = numbers.at(i).isDouble();
            v[std::size_t(i)] = numbers.at(i).toDouble();
        }

        const QcAabb box{{v[0], v[1], v[2]}, {v[3], v[4], v[5]}};
        ok = ok && box.min.x >= 0.0 && box.max.x <= 1.0 && box.min.z >= 0.0 && box.max.z <= 1.0 && box.min.y >= 0.0
             && box.max.y <= maxY && box.min.x < box.max.x && box.min.y < box.max.y && box.min.z < box.max.z;
        if (!ok) {
            reader.fail(u"'%1' entries must be [x0, y0, z0, x1, y1, z1] with 0 <= from < to <= 1 (height up to %2)"_s.arg(key).arg(maxY));
            return {};
        }
        boxes.append(box);
    }
    return boxes;
}

QcShapePart parseShapePart(const QJsonObject &json, QcJsonFieldReader &reader)
{
    QcShapePart part;
    QcJsonFieldReader partReader{json, reader.context, {}};

    part.when = QcShapeParser::parseConditions(json, partReader);
    part.boxes = parseShapeBoxes(json.value(u"boxes"_s), 1.0, u"boxes"_s, partReader);
    part.crosses = parseShapeBoxes(json.value(u"crosses"_s), 1.0, u"crosses"_s, partReader);
    part.customCollision = json.contains(u"collision"_s);
    part.collision = parseShapeBoxes(json.value(u"collision"_s), QcShapeDefinition::maxCollisionHeight, u"collision"_s, partReader);
    part.texture = partReader.string(u"texture"_s);
    part.stretch = partReader.string(u"uv"_s, u"cell"_s) == u"box"_s;

    if (partReader.string(u"uv"_s, u"cell"_s) != u"cell"_s && !part.stretch) {
        partReader.fail(u"'uv' must be cell or box"_s);
    }
    if (part.texture.size() > 32) {
        partReader.fail(u"'texture' names a slot of at most 32 characters"_s);
    }

    reader.adopt(partReader);
    return part;
}

} // namespace

namespace QcShapeParser {

QList<QcShapeCondition> parseConditions(const QJsonObject &json, QcJsonFieldReader &reader)
{
    const QJsonValue when = json.value(u"when"_s);
    if (!when.isUndefined() && (!when.isObject() || when.toObject().size() > QcShapeDefinition::maxConditions)) {
        reader.fail(u"'when' must be an object of at most %1 conditions"_s.arg(QcShapeDefinition::maxConditions));
    }

    QList<QcShapeCondition> result;
    const QJsonObject conditions = when.toObject();
    for (auto it = conditions.constBegin(); it != conditions.constEnd(); ++it) {
        QcShapeCondition condition;
        condition.property = it.key();
        const QJsonArray alternatives = it.value().isArray() ? it.value().toArray() : QJsonArray{it.value()};
        for (const auto &alternative : alternatives) {
            const std::optional<QString> value = QcContentFields::stateValue(alternative);
            if (!value || condition.values.size() >= QcBlockProperty::maxValues) {
                reader.fail(u"'when' values must be strings or booleans"_s);
                break;
            }
            condition.values.append(*value);
        }
        result.append(std::move(condition));
    }
    return result;
}


void parseShapeBody(const QJsonObject &json, QcShapeDefinition &shape, QcJsonFieldReader &reader)
{
    shape.rotate = reader.string(u"rotate"_s);
    shape.flip = reader.string(u"flip"_s);

    const QJsonValue parts = json.value(u"parts"_s);
    if (!parts.isUndefined()) {
        const QJsonArray list = parts.toArray();
        if (!parts.isArray() || list.isEmpty() || list.size() > QcShapeDefinition::maxParts) {
            reader.fail(u"'parts' must be an array of 1..%1 parts"_s.arg(QcShapeDefinition::maxParts));
            return;
        }

        for (const auto &entry : list) {
            if (!entry.isObject()) {
                reader.fail(u"'parts' entries must be objects"_s);
                return;
            }
            shape.parts.append(parseShapePart(entry.toObject(), reader));
        }
    } else {
        shape.parts.append(parseShapePart(json, reader));
    }

    const bool empty = std::ranges::all_of(shape.parts, [](const QcShapePart &part) {
        return part.boxes.isEmpty() && part.crosses.isEmpty();
    });
    if (reader.ok() && empty) {
        reader.fail(u"a shape needs at least one box or cross"_s);
    }
    if (reader.ok() && shape.boxCount() > QcShapeDefinition::maxBoxes) {
        reader.fail(u"a shape has at most %1 boxes, crosses and collision boxes in all parts together"_s.arg(QcShapeDefinition::maxBoxes));
    }
}

QcResult<QcShapeDefinition> parseShape(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"shape"_s, {}};
    QcShapeDefinition shape;
    shape.name = reader.name(u"name"_s, packNamespace);
    if (shape.name.isEmpty()) {
        return qcError(reader.ok() ? u"shape: 'name' is required"_s : reader.error);
    }

    reader.context = u"shape '%1'"_s.arg(shape.name);
    parseShapeBody(json, shape, reader);

    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return shape;
}

} // namespace QcShapeParser
