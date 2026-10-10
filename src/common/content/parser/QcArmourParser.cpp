#include "QcArmourParser.h"

#include "QcContentFields.h"
#include "QcJsonFieldReader.h"

#include <QJsonArray>

using namespace Qt::StringLiterals;

namespace {

constexpr qsizetype maxCovers = QcArmourDefinition::maxCovers;
constexpr qsizetype maxSlotNameLength = 64;

QStringList parseCovers(const QJsonValue &value, QcJsonFieldReader &reader)
{
    QStringList covers;
    const QJsonArray array = value.toArray();
    if (!value.isArray() || array.isEmpty() || array.size() > maxCovers) {
        reader.fail(u"'covers' must be a list of 1 to %1 model slots"_s.arg(maxCovers));
        return covers;
    }

    for (const auto &entry : array) {
        const QString slot = entry.toString();
        if (slot.isEmpty() || slot.size() > maxSlotNameLength) {
            reader.fail(u"'covers' needs slot names of 1 to %1 characters"_s.arg(maxSlotNameLength));
            return {};
        }
        covers.append(slot);
    }
    return covers;
}

QcArmourLayer parseLayer(const QJsonObject &json, const QString &packNamespace, QcJsonFieldReader &reader)
{
    QcArmourLayer layer;
    layer.covers = parseCovers(json.value(u"covers"_s), reader);
    layer.texture = reader.name(u"texture"_s, packNamespace);
    layer.front = reader.name(u"front"_s, packNamespace);
    layer.inflate = reader.number(u"inflate"_s, layer.inflate, 0.0, QcArmourDefinition::maxInflate);
    layer.fromY = reader.number(u"fromY"_s, layer.fromY, -8.0, 8.0);
    layer.toY = reader.number(u"toY"_s, layer.toY, -8.0, 8.0);
    if (layer.texture.isEmpty()) {
        reader.fail(u"layers need a 'texture'"_s);
    }
    if (layer.fromY >= layer.toY) {
        reader.fail(u"layers need 'fromY' < 'toY'"_s);
    }
    return layer;
}

QcArmourPart parsePart(const QJsonObject &json, const QString &packNamespace, QcJsonFieldReader &reader)
{
    QcArmourPart part;
    const std::optional<QcBoneRole> bone = QcContentFields::boneRoleFromName(reader.string(u"bone"_s, u"body"_s));
    const std::optional<QcVec3> from = QcContentFields::vectorField(json.value(u"from"_s));
    const std::optional<QcVec3> to = QcContentFields::vectorField(json.value(u"to"_s));
    part.texture = reader.name(u"texture"_s, packNamespace);
    part.front = reader.name(u"front"_s, packNamespace);
    if (!bone || !from || !to || from->x >= to->x || from->y >= to->y || from->z >= to->z || part.texture.isEmpty()) {
        reader.fail(u"parts need a 'bone' role, 'from' < 'to' (three numbers within 8 m) and a 'texture'"_s);
        return part;
    }

    part.bone = *bone;
    part.from = *from;
    part.to = *to;
    return part;
}

// Reads the array at `key` (at most `limit` objects) with `parse(object, nested reader)`.
template<typename Entry, typename Parse>
QList<Entry> parseList(const QJsonObject &json, const QString &key, qsizetype limit, QcJsonFieldReader &reader, Parse parse)
{
    QList<Entry> entries;
    const QJsonValue value = json.value(key);
    if (value.isUndefined()) {
        return entries;
    }
    if (!value.isArray() || value.toArray().size() > limit) {
        reader.fail(u"'%1' must be a list of at most %2 objects"_s.arg(key).arg(limit));
        return entries;
    }

    const QJsonArray array = value.toArray();
    for (const auto &entry : array) {
        const QJsonObject object = entry.toObject();
        QcJsonFieldReader nested{object, reader.context + u' ' + key, {}};
        entries.append(parse(object, nested));
        reader.adopt(nested);
    }
    return entries;
}

} // namespace

namespace QcArmourParser {

QcArmourDefinition parseArmour(const QJsonObject &json, const QString &packNamespace, const QString &itemName, QcJsonFieldReader &reader)
{
    QcArmourDefinition armour;
    const std::optional<QcEquipmentSlot> slot = qcEquipmentSlotFromName(reader.string(u"slot"_s));
    if (!slot) {
        reader.fail(u"'slot' must be head, chest, legs or feet"_s);
    }
    armour.slot = slot.value_or(QcEquipmentSlot::Head);
    armour.points = reader.integer(u"points"_s, 0, 0, QcArmourDefinition::maxPoints);
    armour.set = reader.name(u"set"_s, packNamespace, itemName);

    armour.layers = parseList<QcArmourLayer>(json, u"layers"_s, QcArmourDefinition::maxLayers, reader,
                                             [&](const QJsonObject &object, QcJsonFieldReader &nested) { return parseLayer(object, packNamespace, nested); });
    armour.parts = parseList<QcArmourPart>(json, u"parts"_s, QcArmourDefinition::maxParts, reader,
                                           [&](const QJsonObject &object, QcJsonFieldReader &nested) { return parsePart(object, packNamespace, nested); });
    return armour;
}

} // namespace QcArmourParser
