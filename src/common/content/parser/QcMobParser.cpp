#include "common/content/parser/QcMobParser.h"

#include "common/content/parser/QcContentFields.h"
#include "common/content/parser/QcJsonFieldReader.h"
#include "common/content/parser/QcMobReader.h"

#include <QJsonArray>

using namespace Qt::StringLiterals;

namespace {

QList<QcModelBox> parseBoxes(const QJsonValue &value, int limit, QcJsonFieldReader &reader)
{
    QList<QcModelBox> boxes;
    if (!value.isArray() || value.toArray().size() > limit) {
        reader.fail(u"'boxes' must be an array of at most %1 boxes"_s.arg(limit));
        return boxes;
    }

    const QJsonArray list = value.toArray();
    for (const auto &entry : list) {
        const QJsonObject boxJson = entry.toObject();
        const std::optional<QcVec3> from = QcContentFields::vectorField(boxJson.value(u"from"_s));
        const std::optional<QcVec3> to = QcContentFields::vectorField(boxJson.value(u"to"_s));
        const QString slot = boxJson.value(u"slot"_s).toString();
        if (!from || !to || slot.isEmpty() || slot.size() > 64 || from->x >= to->x || from->y >= to->y || from->z >= to->z) {
            reader.fail(u"boxes need 'from' < 'to' (three numbers within 8 m) and a 'slot'"_s);
            return {};
        }
        boxes.append({*from, *to, slot});
    }
    return boxes;
}

[[nodiscard]] std::optional<QcTint> tintFromName(const QString &name)
{
    static const QHash<QString, QcTint> tints = {
        {u"none"_s, QcTint::None}, {u"skin"_s, QcTint::Skin}, {u"hair"_s, QcTint::Hair},
        {u"shirt"_s, QcTint::Shirt}, {u"pants"_s, QcTint::Pants},
    };
    const auto it = tints.constFind(name);
    return it == tints.constEnd() ? std::nullopt : std::optional<QcTint>(*it);
}

[[nodiscard]] QcResult<QcModelBone> parseBone(const QJsonObject &boneJson, const QString &context)
{
    QcJsonFieldReader boneReader{boneJson, context + u" bone"_s, {}};
    QcModelBone bone;
    bone.name = boneReader.string(u"name"_s);

    const QString role = boneReader.string(u"role"_s, u"none"_s);
    const std::optional<QcBoneRole> parsedRole = QcContentFields::boneRoleFromName(role);
    if (!parsedRole) {
        boneReader.fail(u"'role' must be none, head, body, right_arm, left_arm, right_leg or left_leg"_s);
    }
    bone.role = parsedRole.value_or(QcBoneRole::None);

    const std::optional<QcVec3> pivot = QcContentFields::vectorField(boneJson.value(u"pivot"_s));
    if (!pivot) {
        boneReader.fail(u"'pivot' must be three numbers within 8 m"_s);
    }
    bone.pivot = pivot.value_or(QcVec3{});

    bone.boxes = parseBoxes(boneJson.value(u"boxes"_s), QcModelDefinition::maxBoxesPerBone, boneReader);
    if (bone.name.isEmpty() || bone.name.size() > 64 || bone.boxes.isEmpty()) {
        boneReader.fail(u"bones need a 'name' (at most 64 characters) and at least one box"_s);
    }

    if (!boneReader.ok()) {
        return qcError(boneReader.error);
    }
    return bone;
}

[[nodiscard]] QList<QcAppearanceLayer> parseAppearanceLayers(const QJsonObject &json, QcJsonFieldReader &reader,
                                                             const QString &packNamespace)
{
    QList<QcAppearanceLayer> result;
    const QJsonValue layers = json.value(u"layers"_s);
    if (!layers.isObject() || layers.toObject().isEmpty() || layers.toObject().size() > QcAppearanceDefinition::maxLayers) {
        reader.fail(u"'layers' must map 1 to %1 slots to textures"_s.arg(QcAppearanceDefinition::maxLayers));
    }

    const QJsonObject layerMap = layers.toObject();
    for (auto it = layerMap.constBegin(); it != layerMap.constEnd() && reader.ok(); ++it) {
        const QJsonObject layerJson = it.value().toObject();
        QcJsonFieldReader layerReader{layerJson, u"%1 layer '%2'"_s.arg(reader.context, it.key()), {}};
        QcAppearanceLayer layer;
        layer.slot = it.key();
        layer.texture = layerReader.name(u"texture"_s, packNamespace);
        const std::optional<QcTint> tint = tintFromName(layerReader.string(u"tint"_s, u"none"_s));
        if (!tint) {
            layerReader.fail(u"'tint' must be none, skin, hair, shirt or pants"_s);
        }
        layer.tint = tint.value_or(QcTint::None);
        if (!it.value().isObject() || layer.texture.isEmpty() || layer.slot.size() > 64) {
            layerReader.fail(u"needs a 'texture'"_s);
        }

        reader.adopt(layerReader);
        result.append(std::move(layer));
    }
    return result;
}

[[nodiscard]] std::optional<QcAppearanceDefinition::Kind> appearanceKindFromName(const QString &name)
{
    static const QHash<QString, QcAppearanceDefinition::Kind> kinds = {
        {u"outfit"_s, QcAppearanceDefinition::Kind::Outfit},
        {u"hair"_s, QcAppearanceDefinition::Kind::Hair},
        {u"cape"_s, QcAppearanceDefinition::Kind::Cape},
    };
    const auto it = kinds.constFind(name);
    return it == kinds.constEnd() ? std::nullopt : std::optional(*it);
}

[[nodiscard]] QcCapeShape parseCapeShape(QcJsonFieldReader &reader)
{
    QcCapeShape shape;
    shape.width = reader.number(u"width"_s, shape.width, QcCapeShape::minSize, QcCapeShape::maxWidth);
    shape.length = reader.number(u"length"_s, shape.length, QcCapeShape::minSize, QcCapeShape::maxLength);
    shape.thickness = reader.number(u"thickness"_s, shape.thickness, 0.005, QcCapeShape::maxThickness);
    shape.offset = reader.number(u"offset"_s, shape.offset, 0.0, QcCapeShape::maxOffset);
    return shape;
}

} // namespace

namespace QcMobParser {

[[nodiscard]] QcResult<QcMobDefinition> parseMob(const QJsonObject &json, const QString &packNamespace)
{
    return QcMobReader(json, packNamespace).read();
}

[[nodiscard]] QcResult<QcModelDefinition> parseModel(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"model"_s, {}};
    QcModelDefinition model;
    model.name = reader.name(u"name"_s, packNamespace);
    if (model.name.isEmpty()) {
        return qcError(reader.ok() ? u"model: 'name' is required"_s : reader.error);
    }

    reader.context = u"model '%1'"_s.arg(model.name);
    const QJsonValue bones = json.value(u"bones"_s);
    if (!bones.isArray() || bones.toArray().isEmpty() || bones.toArray().size() > QcModelDefinition::maxBones) {
        return qcError(u"%1: 'bones' must be an array of 1 to %2 bones"_s.arg(reader.context).arg(QcModelDefinition::maxBones));
    }

    const QJsonArray boneList = bones.toArray();
    for (const auto &entry : boneList) {
        QcResult<QcModelBone> bone = parseBone(entry.toObject(), reader.context);
        if (!bone) {
            return qcError(bone.error());
        }
        model.bones.append(std::move(*bone));
    }
    return model;
}

[[nodiscard]] QcResult<QcAppearanceDefinition> parseAppearance(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"appearance"_s, {}};
    QcAppearanceDefinition appearance;
    appearance.name = reader.name(u"name"_s, packNamespace);
    if (appearance.name.isEmpty()) {
        return qcError(reader.ok() ? u"appearance: 'name' is required"_s : reader.error);
    }

    reader.context = u"appearance '%1'"_s.arg(appearance.name);
    appearance.translationKey = reader.string(u"translation"_s, QcContentFields::translationKey(u"appearance"_s, appearance.name));

    const std::optional<QcAppearanceDefinition::Kind> kind = appearanceKindFromName(reader.string(u"kind"_s, u"outfit"_s));
    if (!kind) {
        reader.fail(u"'kind' must be outfit, hair or cape"_s);
    }
    appearance.kind = kind.value_or(QcAppearanceDefinition::Kind::Outfit);
    appearance.playable = reader.boolean(u"playable"_s, true);
    if (appearance.kind == QcAppearanceDefinition::Kind::Cape) {
        reader.nestedObject(u"shape"_s, [&appearance](const QJsonObject &, QcJsonFieldReader &nested) { appearance.cape = parseCapeShape(nested); });
    }

    appearance.layers = parseAppearanceLayers(json, reader, packNamespace);
    if (appearance.kind == QcAppearanceDefinition::Kind::Hair) {
        appearance.boxes = parseBoxes(json.value(u"boxes"_s), QcAppearanceDefinition::maxBoxes, reader);
        if (reader.ok() && appearance.boxes.isEmpty()) {
            reader.fail(u"hair styles need 'boxes'"_s);
        }
    }
    if (reader.ok() && appearance.kind == QcAppearanceDefinition::Kind::Cape && !appearance.layer(u"outside"_s)) {
        reader.fail(u"capes need an 'outside' layer"_s);
    }

    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return appearance;
}

} // namespace QcMobParser
