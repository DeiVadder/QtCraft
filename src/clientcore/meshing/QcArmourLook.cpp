#include "clientcore/meshing/QcArmourLook.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcArmour.h"

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace QcArmourLook {

namespace {

// The box grown by `inflate` on every side and cut to the layer's heights (after growing, so a cut
// edge stays flush with the cut and does not stick out below or above).
[[nodiscard]] std::optional<QcAabb> layerBox(const QcModelBox &box, const QcArmourLayer &layer)
{
    const QcVec3 grow{layer.inflate, layer.inflate, layer.inflate};
    QcAabb result{box.from - grow, box.to + grow};
    result.min.y = std::max(result.min.y, layer.fromY);
    result.max.y = std::min(result.max.y, layer.toY);
    if (result.min.y >= result.max.y) {
        return std::nullopt;
    }
    return result;
}

// Attachments gathered into one list.
class Attachments
{
public:
    // Adds the layer boxes of the boxes of `bone` that `layer` covers.
    void addCovered(const QcArmourLayer &layer, const QcModelBone &bone)
    {
        for (const QcModelBox &box : bone.boxes) {
            const std::optional<QcAabb> grown = layer.covers.contains(box.slot) ? layerBox(box, layer) : std::nullopt;
            if (grown) {
                m_list.append({bone.role, *grown, layer.texture, layer.front});
            }
        }
    }

    [[nodiscard]] QList<QcEntityMesher::Attachment> take() { return std::exchange(m_list, {}); }

private:
    QList<QcEntityMesher::Attachment> m_list;
};

} // namespace

const QcModelDefinition *referenceModel(const QcContentRegistry &content)
{
    return content.assets().model(u"qtcraft:humanoid"_s);
}

QList<QcEntityMesher::Attachment> layers(const QcArmourDefinition &armour, const QcModelDefinition &reference)
{
    Attachments result;
    for (const QcArmourLayer &layer : armour.layers) {
        for (const QcModelBone &bone : reference.bones) {
            result.addCovered(layer, bone);
        }
    }
    return result.take();
}

QList<QcEntityMesher::Attachment> attachments(const QcContentRegistry &content, const QcEquipment &equipment)
{
    QList<QcEntityMesher::Attachment> result;
    const QcModelDefinition *reference = referenceModel(content);
    for (const QcEquipmentSlot slot : {QcEquipmentSlot::Feet, QcEquipmentSlot::Legs, QcEquipmentSlot::Chest, QcEquipmentSlot::Head}) {
        const QcItemStack &stack = equipment.at(slot);
        const QcArmourDefinition *armour = stack.isEmpty() ? nullptr : QcArmour::of(content, stack.item);
        if (!armour) {
            continue;
        }

        if (reference) {
            result.append(layers(*armour, *reference));
        }
        for (const QcArmourPart &part : armour->parts) {
            result.append({part.bone, {part.from, part.to}, part.texture, part.front});
        }
    }
    return result;
}

QList<QcEntityMesher::Attachment> headFillers(const QcContentRegistry &content, const QcEquipment &equipment, const QString &texture)
{
    const QcItemStack &head = equipment.at(QcEquipmentSlot::Head);
    const QcArmourDefinition *armour = head.isEmpty() ? nullptr : QcArmour::of(content, head.item);
    const QcModelDefinition *reference = referenceModel(content);
    if (!armour || !reference) {
        return {};
    }

    QcArmourDefinition filler = *armour;
    for (QcArmourLayer &layer : filler.layers) {
        layer.inflate = 0.0;
        layer.texture = texture;
        layer.front.clear();
    }
    return layers(filler, *reference);
}

double capeClearance(const QcModelDefinition &wearer, const QList<QcEntityMesher::Attachment> &attachments)
{
    const auto body = std::ranges::find(wearer.bones, QcBoneRole::Body, &QcModelBone::role);
    if (body == wearer.bones.end() || body->boxes.isEmpty()) {
        return 0.0;
    }

    const double back = std::ranges::max(body->boxes, {}, [](const QcModelBox &box) { return box.to.z; }).to.z;
    double clearance = 0.0;
    for (const QcEntityMesher::Attachment &attachment : attachments) {
        if (attachment.bone == QcBoneRole::Body) {
            clearance = std::max(clearance, attachment.box.max.z - back);
        }
    }
    return clearance;
}

} // namespace QcArmourLook
