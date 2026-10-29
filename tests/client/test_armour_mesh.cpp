// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/meshing/QcAnimator.h"
#include "clientcore/meshing/QcArmourLook.h"
#include "clientcore/meshing/QcEntityMesher.h"
#include "clientcore/meshing/QcFigureLook.h"
#include "clientcore/preview/QcPreviewScene.h"
#include "clientcore/session/QcClientEntities.h"
#include "QcTestUtil.h"

#include <QTest>

#include <algorithm>
#include <limits>

using namespace Qt::StringLiterals;

namespace {

// Every texture gets its own little rectangle, so tests can tell faces apart by their UVs.
QRectF uvOf(const QString &texture)
{
    const double u = static_cast<double>(qHash(texture) % 1000) / 1000.0;
    return {u, 0.0, 0.0005, 0.0005};
}

constexpr qsizetype verticesPerBox = 24;

// The vertices drawn with one of `textures` (by their rectangles from uvOf).
QList<QcEntityVertex> drawnWith(const QcEntityMesh &mesh, const QStringList &textures)
{
    QList<QcEntityVertex> found;
    for (const QcEntityVertex &vertex : mesh.vertices) {
        const bool match = std::ranges::any_of(textures, [&vertex](const QString &texture) {
            const QRectF rect = uvOf(texture);
            return static_cast<double>(vertex.u) >= rect.left() - 1e-6 && static_cast<double>(vertex.u) <= rect.right() + 1e-6
                && static_cast<double>(vertex.v) <= rect.bottom() + 1e-6;
        });
        if (match) {
            found.append(vertex);
        }
    }
    return found;
}

const QStringList capeTextures{u"qtcraft:cape_green_emblem"_s, u"qtcraft:cape_green_outside"_s, u"qtcraft:cape_green_inside"_s,
                               u"qtcraft:cape_green_edge"_s};

// How far the cape reaches towards the back's body (its nearest point) and how far the iron layer
// reaches behind (its farthest point); the figure faces -Z.
std::pair<double, double> capeAndLayer(const QcEntityMesh &mesh)
{
    const QList<QcEntityVertex> cape = drawnWith(mesh, capeTextures);
    const QList<QcEntityVertex> layer = drawnWith(mesh, {u"qtcraft:iron_armour"_s});
    if (cape.isEmpty() || layer.isEmpty()) {
        return {0.0, 1.0};
    }
    return {static_cast<double>(std::ranges::min(cape, {}, &QcEntityVertex::z).z), static_cast<double>(std::ranges::max(layer, {}, &QcEntityVertex::z).z)};
}

QcAabb boundsOf(const QcEntityMesh &mesh, qsizetype first, qsizetype count)
{
    QcAabb box{{1e9, 1e9, 1e9}, {-1e9, -1e9, -1e9}};
    for (qsizetype i = first; i < first + count; ++i) {
        const QcEntityVertex &v = mesh.vertices[i];
        box.min = {std::min(box.min.x, static_cast<double>(v.x)), std::min(box.min.y, static_cast<double>(v.y)), std::min(box.min.z, static_cast<double>(v.z))};
        box.max = {std::max(box.max.x, static_cast<double>(v.x)), std::max(box.max.y, static_cast<double>(v.y)), std::max(box.max.z, static_cast<double>(v.z))};
    }
    return box;
}

} // namespace

// What worn armour looks like (decision E-235) and armour stands (E-238): layer boxes from the
// reference model, parts on the bones, the same animation as the body, hidden stand bones, and the
// client's view of stands.
class TestArmourMesh : public QObject
{
    Q_OBJECT

    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    QcItemId item(const char *name) const { return *m_content->itemId(u"qtcraft:"_s + QString::fromLatin1(name)); }

    QcEquipment set(const char *material) const
    {
        QcEquipment equipment;
        for (const char *piece : {"helmet", "chestplate", "leggings", "boots"}) {
            const QcItemId id = item(qPrintable(QString::fromLatin1(material) + u'_' + QString::fromLatin1(piece)));
            equipment.at(m_content->item(id).armour->slot) = {id, 1, 0};
        }
        return equipment;
    }

    QcEntityMesh playerMesh(const QcEquipment &equipment, const QcEntityPose &pose = {}) const
    {
        QcPreviewScene::Figure figure;
        figure.equipment = equipment;
        figure.pose = pose;
        return QcPreviewScene::build(*m_content, figure, uvOf);
    }

private slots:
    void layersAreGrownBoxesOfTheReferenceModel()
    {
        QcEquipment helmetOnly;
        helmetOnly.at(QcEquipmentSlot::Head) = {item("iron_helmet"), 1, 0};
        const QList<QcEntityMesher::Attachment> attachments = QcArmourLook::attachments(*m_content, helmetOnly);
        QCOMPARE(attachments.size(), 1);
        const QcEntityMesher::Attachment &layer = attachments.constFirst();
        QCOMPARE(layer.bone, QcBoneRole::Head);
        // The humanoid head (0.45 m cube from y 1.35) made 4 cm larger on every side.
        QCOMPARE(layer.box.min, (QcVec3{-0.265, 1.31, -0.265}));
        QCOMPARE(layer.box.max, (QcVec3{0.265, 1.84, 0.265}));
        QCOMPARE(layer.texture, u"qtcraft:iron_armour"_s);
        QCOMPARE(layer.front, u"qtcraft:iron_helmet_front"_s);

        // Boots cover the shoes and the shins up to their cut; leggings the legs, a bit tighter.
        QcEquipment legs;
        legs.at(QcEquipmentSlot::Legs) = {item("leather_leggings"), 1, 0};
        legs.at(QcEquipmentSlot::Feet) = {item("leather_boots"), 1, 0};
        const QList<QcEntityMesher::Attachment> lower = QcArmourLook::attachments(*m_content, legs);
        QCOMPARE(lower.size(), 6);
        double bootsTop = 0.0;
        double bootsWidth = 0.0;
        double leggingsWidth = 0.0;
        for (const QcEntityMesher::Attachment &attachment : lower) {
            QVERIFY(attachment.bone == QcBoneRole::LeftLeg || attachment.bone == QcBoneRole::RightLeg);
            if (attachment.box.max.y <= 0.33) {
                bootsTop = std::max(bootsTop, attachment.box.max.y);
                bootsWidth = std::max(bootsWidth, attachment.box.max.z - attachment.box.min.z);
            } else {
                leggingsWidth = std::max(leggingsWidth, attachment.box.max.z - attachment.box.min.z);
            }
        }
        QCOMPARE(bootsTop, 0.32);
        QVERIFY(bootsWidth > leggingsWidth);
        // Feet come first in the list.
        QVERIFY(lower.constFirst().box.max.y <= 0.33);
    }

    void partsSitOnTheirBones()
    {
        QcEquipment equipment;
        equipment.at(QcEquipmentSlot::Chest) = {item("iron_chestplate"), 1, 0};
        const QList<QcEntityMesher::Attachment> attachments = QcArmourLook::attachments(*m_content, equipment);
        const auto onBone = [&attachments](QcBoneRole bone) {
            return std::ranges::count_if(attachments, [bone](const auto &attachment) { return attachment.bone == bone; });
        };
        // Torso layer on the body, a sleeve layer and a shoulder piece on each arm.
        QCOMPARE(onBone(QcBoneRole::Body), 1);
        QCOMPARE(onBone(QcBoneRole::RightArm), 2);
        QCOMPARE(onBone(QcBoneRole::LeftArm), 2);
    }

    void wornArmourAddsItsBoxesToTheFigure()
    {
        const QcEntityMesh bare = playerMesh({});
        const QcEntityMesh iron = playerMesh(set("iron"));
        QVERIFY(!bare.vertices.isEmpty());
        const qsizetype boxes = QcArmourLook::attachments(*m_content, set("iron")).size();
        // Under the helmet the hair is left out, so it cannot poke through.
        const QcAppearanceDefinition *hair = m_content->assets().appearance(QcAppearance().hair);
        QVERIFY(hair);
        QCOMPARE(iron.vertices.size(), bare.vertices.size() + (boxes - hair->boxes.size()) * verticesPerBox);

        // Unknown items (a mod gone) draw nothing.
        QcEquipment unknown;
        unknown.at(QcEquipmentSlot::Head) = {static_cast<QcItemId>(60000), 1, 0};
        QCOMPARE(playerMesh(unknown).vertices.size(), bare.vertices.size());
    }

    void armourMovesWithTheAnimation()
    {
        QcEquipment boots;
        boots.at(QcEquipmentSlot::Feet) = {item("diamond_boots"), 1, 0};
        QcEntityPose walking;
        walking.speed = 4.3;
        walking.walkDistance = 0.4;
        const QcEntityMesh standing = playerMesh(boots);
        const QcEntityMesh striding = playerMesh(boots, walking);
        QCOMPARE(standing.vertices.size(), striding.vertices.size());

        // The armour boxes are the last ones; in a stride the boots swing with the legs.
        const qsizetype armour = QcArmourLook::attachments(*m_content, boots).size() * verticesPerBox;
        const qsizetype first = standing.vertices.size() - armour;
        const QcAabb still = boundsOf(standing, first, armour);
        const QcAabb moving = boundsOf(striding, first, armour);
        QVERIFY(moving.max.z - moving.min.z > still.max.z - still.min.z + 0.05);
    }

    void standsHideTheirArmsAndPlate()
    {
        QcStandPose pose;
        QcEntityMesher::ModelLook look = QcFigureLook::stand(*m_content, pose);
        QVERIFY(look.isValid());
        const QcEntityMesher::Placement placement{{}, 0.0, QcEntityLight(1.0f, 0.0f), false};

        const auto build = [&](const QcStandPose &standPose, const QcEquipment &equipment) {
            QcEntityMesh mesh;
            const QcFigureLook::Dressed dressed = QcFigureLook::armour(*m_content, equipment, QcFigureLook::stand(*m_content, standPose));
            QcEntityMesher::appendModel(mesh, {dressed.look, nullptr, &dressed.armour, QcFigureLook::standHiddenBones(standPose)},
                                        QcFigureLook::standBody(standPose, 0.0), placement, uvOf);
            return mesh;
        };

        const qsizetype full = build(pose, {}).vertices.size();
        pose.arms = false;
        QCOMPARE(build(pose, {}).vertices.size(), full - 2 * verticesPerBox);
        pose.basePlate = false;
        QCOMPARE(build(pose, {}).vertices.size(), full - 3 * verticesPerBox);

        // Without arms the chestplate keeps its torso but loses sleeves and shoulder pieces.
        QcEquipment chest;
        chest.at(QcEquipmentSlot::Chest) = {item("iron_chestplate"), 1, 0};
        QCOMPARE(build(pose, chest).vertices.size(), full - 3 * verticesPerBox + verticesPerBox);

        // A small stand is half as tall.
        QcStandPose small;
        small.small = true;
        const QcEntityMesh smallMesh = build(small, {});
        QVERIFY(boundsOf(smallMesh, 0, smallMesh.vertices.size()).max.y < 1.0);
    }

    void capeHangsOutsideTheChestLayer()
    {
        // The chest layer's thickness becomes the cape's clearance (decisions E-235, E-240) ...
        QcEquipment chest;
        chest.at(QcEquipmentSlot::Chest) = {item("iron_chestplate"), 1, 0};
        QcAppearance appearance;
        appearance.cape = u"qtcraft:green_cape"_s;
        QVERIFY(QcFigureLook::player(*m_content, appearance).cape);
        const QcFigureLook::Dressed dressed = QcFigureLook::armour(*m_content, chest, QcFigureLook::player(*m_content, appearance));
        QcEntityMesher::ModelLook look = dressed.look;
        const QList<QcEntityMesher::Attachment> &armour = dressed.armour;
        QVERIFY(std::abs(look.capeClearance - 0.035) < 1e-9);
        QcEquipment boots;
        boots.at(QcEquipmentSlot::Feet) = {item("iron_boots"), 1, 0};
        QCOMPARE(QcFigureLook::armour(*m_content, boots, QcFigureLook::player(*m_content, appearance)).look.capeClearance, 0.0);

        // ... so in the preview (the same path as players in the world) the cape lies outside it.
        QcPreviewScene::Figure figure;
        figure.appearance = appearance;
        figure.equipment = chest;
        const auto [capeFront, layerBack] = capeAndLayer(QcPreviewScene::build(*m_content, figure, uvOf));
        QVERIFY2(capeFront > layerBack + 0.01, qPrintable(u"%1 %2"_s.arg(capeFront).arg(layerBack)));

        // Without the clearance it would cut through the layer.
        const QcEntityMesher::Placement placement{{}, 0.0, QcEntityLight(1.0f, 0.0f), false};
        look.capeClearance = 0.0;
        QcEntityMesh tight;
        QcEntityMesher::appendModel(tight, {look, nullptr, &armour}, {}, placement, uvOf);
        const auto [tightFront, tightBack] = capeAndLayer(tight);
        QVERIFY(tightFront < tightBack);

        // A stand with a cape (E-239): its pole is thin, the layer has the humanoid's size, and the
        // cape still hangs outside the layer on the body (the stand's arms are posed, so their
        // sleeves are left out of the comparison).
        QcEntityMesher::ModelLook caped = QcFigureLook::stand(*m_content, {});
        caped.cape = QcFigureLook::cape(*m_content, appearance.cape);
        const QcFigureLook::Dressed stand = QcFigureLook::armour(*m_content, chest, caped);
        const QList<QcEntityMesher::Attachment> &worn = stand.armour;
        QVERIFY(stand.look.capeClearance > 0.1);
        QcEntityMesh standMesh;
        QcEntityMesher::appendModel(standMesh, {stand.look, nullptr, &worn}, QcFigureLook::standBody({}, 0.0), placement, uvOf);
        const double standFront = std::ranges::min(drawnWith(standMesh, capeTextures), {}, &QcEntityVertex::z).z;
        double standBack = 0.0;
        for (const QcEntityMesher::Attachment &attachment : worn) {
            standBack = attachment.bone == QcBoneRole::Body ? std::max(standBack, attachment.box.max.z) : standBack;
        }
        QVERIFY(standBack > 0.15);
        QVERIFY2(standFront > standBack + 0.01, qPrintable(u"%1 %2"_s.arg(standFront).arg(standBack)));
    }

    void helmetsOnStandsAreFilledBehindTheFaceOpening()
    {
        QcEquipment helmet;
        helmet.at(QcEquipmentSlot::Head) = {item("iron_helmet"), 1, 0};
        const QcEntityMesher::ModelLook stand = QcFigureLook::stand(*m_content, {});
        const QcFigureLook::Dressed dressed = QcFigureLook::standArmour(*m_content, helmet, stand);
        const QList<QcEntityMesher::Attachment> &worn = dressed.armour;

        // The helmet plus one filler: the humanoid head (0.45 m cube from y 1.35) in the wood of
        // the stand's outfit, closed all round, inside the helmet.
        QCOMPARE(worn.size(), 2);
        const auto filler = std::ranges::find(worn, u"qtcraft:armor_stand_wood"_s, &QcEntityMesher::Attachment::texture);
        QVERIFY(filler != worn.end());
        QCOMPARE(filler->bone, QcBoneRole::Head);
        QVERIFY(filler->front.isEmpty());
        QCOMPARE(filler->box.min, (QcVec3{-0.225, 1.35, -0.225}));
        QCOMPARE(filler->box.max, (QcVec3{0.225, 1.8, 0.225}));

        // Through the opening of the front texture the filler's front face is seen, drawn in wood.
        QcEntityMesh mesh;
        QcEntityMesher::appendModel(mesh, {dressed.look, nullptr, &worn}, QcFigureLook::standBody({}, 0.0), {}, uvOf);
        const QList<QcEntityVertex> wood = drawnWith(mesh, {u"qtcraft:armor_stand_wood"_s});
        QVERIFY(std::ranges::any_of(wood, [](const QcEntityVertex &vertex) { return std::abs(vertex.z + 0.225) < 1e-4 && vertex.y > 1.5; }));

        // Players and stands without a helmet get no filler.
        QCOMPARE(QcFigureLook::armour(*m_content, helmet, QcFigureLook::player(*m_content, {})).armour.size(), 1);
        QcEquipment chest;
        chest.at(QcEquipmentSlot::Chest) = {item("iron_chestplate"), 1, 0};
        QCOMPARE(QcFigureLook::standArmour(*m_content, chest, stand).armour.size(), QcFigureLook::armour(*m_content, chest, stand).armour.size());
    }

    void standPoseTurnsTheBones()
    {
        QcStandPose pose;
        pose.at(QcStandPose::Part::RightArm) = {90.0f, 0.0f, 0.0f};
        pose.at(QcStandPose::Part::Head) = {0.0f, 45.0f, -10.0f};
        const QcAnimator::BodyPose body = QcFigureLook::standBody(pose, 0.0);
        QCOMPARE(body.bone(QcBoneRole::RightArm).pitch, 90.0);
        QCOMPARE(body.bone(QcBoneRole::Head).yaw, 45.0);
        QCOMPARE(body.bone(QcBoneRole::Head).roll, -10.0);
        QCOMPARE(body.bone(QcBoneRole::LeftArm).pitch, -10.0);
        QCOMPARE(body.bodyPitch, 0.0);
    }

    void clientsKeepStandsWithTheirLook()
    {
        QcClientEntities entities;
        QcMsg::EntitySpawn spawn;
        spawn.id = 9;
        spawn.kind = QcEntityKind::ArmourStand;
        spawn.name = u"Tin"_s;
        spawn.yaw = 90.0f;
        spawn.equipment.at(QcEquipmentSlot::Chest) = {item("iron_chestplate"), 1, 4};
        spawn.equipment.at(QcEquipmentSlot::Head) = {static_cast<QcItemId>(60000), 1, 0};
        spawn.stand.small = true;
        QVERIFY(entities.spawn(spawn, m_content.get()));

        const QcClientEntity &stand = entities.all().value(9);
        QVERIFY(stand.isArmourStand());
        QVERIFY(stand.stand.small);
        QCOMPARE(stand.equipment.at(QcEquipmentSlot::Chest).damage, 4);
        // Unknown items are dropped before anything is drawn from them.
        QVERIFY(stand.equipment.at(QcEquipmentSlot::Head).isEmpty());

        // A changed stand comes again: pose, armour and rotation follow.
        spawn.stand.small = false;
        spawn.yaw = 180.0f;
        spawn.equipment = {};
        QVERIFY(entities.spawn(spawn, m_content.get()));
        QVERIFY(!entities.all().value(9).stand.small);
        QCOMPARE(entities.all().value(9).yaw, 180.0f);
        QVERIFY(entities.all().value(9).equipment.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestArmourMesh)
#include "QcTestUtil.h"
#include "test_armour_mesh.moc"
