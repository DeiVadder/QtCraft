#include "common/content/parser/QcContentParser.h"
#include "common/content/registry/QcArmourSerializer.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/content/registry/QcContentSerializer.h"
#include "common/gameplay/QcArmour.h"
#include "common/gameplay/QcQuickMove.h"
#include "common/gameplay/QcRepair.h"
#include "common/gameplay/QcStandPose.h"
#include "common/protocol/QcMessages.h"
#include "QcTestUtil.h"
#include "server/gameplay/QcStandProperties.h"

#include <QJsonDocument>
#include <QTest>

#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace {

QJsonObject json(const char *text)
{
    return QJsonDocument::fromJson(QByteArray(text)).object();
}

} // namespace

// Armour as data and its rules without a server (decisions E-234 to E-236, E-238, E-239): the
// "armour" object of items, its content sync, damage reduction, wear, repair, quick move targets,
// the stand pose and the stand properties of commands and structures.
class TestArmour : public QObject
{
    Q_OBJECT

    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    QcItemId item(const char *name) const { return *m_content->itemId(u"qtcraft:"_s + QString::fromLatin1(name)); }

private slots:
    void armourIsParsedFromItems()
    {
        const auto helmet = QcContentParser::parseItem(json(R"({"name":"helm","durability":100,"maxStack":16,"armour":{
            "slot":"head","points":3,
            "layers":[{"covers":["head"],"texture":"plate","front":"open","inflate":0.05,"fromY":1.0,"toY":2.0}],
            "parts":[{"bone":"head","from":[-0.1,1.8,-0.1],"to":[0.1,1.9,0.1],"texture":"crest"}]}})"),
                                                        u"m"_s);
        QVERIFY2(helmet.has_value(), qcErrorText(helmet).constData());
        QVERIFY(helmet->armour.has_value());
        const QcArmourDefinition &armour = *helmet->armour;
        QCOMPARE(armour.slot, QcEquipmentSlot::Head);
        QCOMPARE(armour.points, 3);
        // Without a set the item is its own set; names get the pack's namespace.
        QCOMPARE(armour.set, u"m:helm"_s);
        QCOMPARE(armour.layers.size(), 1);
        QCOMPARE(armour.layers[0].covers, QStringList{u"head"_s});
        QCOMPARE(armour.layers[0].texture, u"m:plate"_s);
        QCOMPARE(armour.layers[0].front, u"m:open"_s);
        QCOMPARE(armour.layers[0].inflate, 0.05);
        QCOMPARE(armour.parts.size(), 1);
        QCOMPARE(armour.parts[0].bone, QcBoneRole::Head);
        QCOMPARE(armour.parts[0].to, (QcVec3{0.1, 1.9, 0.1}));
        // Armour is worn one at a time: it never stacks, whatever the data says.
        QCOMPARE(helmet->maxStack, 1);

        const auto plain = QcContentParser::parseItem(json(R"({"name":"stick"})"), u"m"_s);
        QVERIFY(plain.has_value() && !plain->armour && !plain->placesStand);
        const auto stand = QcContentParser::parseItem(json(R"({"name":"stand","placesStand":true})"), u"m"_s);
        QVERIFY(stand.has_value() && stand->placesStand);
    }

    void iconsAndStandItemComeFromTheData()
    {
        // The base content: the first piece per slot in content order, the strongest chestplate,
        // the stand item.
        for (const QcEquipmentSlot slot : {QcEquipmentSlot::Head, QcEquipmentSlot::Chest, QcEquipmentSlot::Legs, QcEquipmentSlot::Feet}) {
            const std::optional<QcItemId> placeholder = QcArmour::placeholder(*m_content, slot);
            QVERIFY(placeholder.has_value());
            QVERIFY(QcArmour::fits(*m_content, {*placeholder, 1, 0}, slot));
        }
        QCOMPARE(QcArmour::pointsIcon(*m_content), std::optional(item("diamond_chestplate")));
        QCOMPARE(QcStandProperties::standItem(*m_content), u"qtcraft:armor_stand"_s);

        // A mod's content without any base items: its own pieces and stand.
        QcContentRegistry modded;
        const auto add = [&modded](const char *text) {
            const auto parsed = QcContentParser::parseItem(json(text), u"m"_s);
            QVERIFY2(parsed.has_value(), qcErrorText(parsed).constData());
            QVERIFY(modded.registerItem(*parsed).has_value());
        };
        add(R"({"name":"weak_vest","armour":{"slot":"chest","points":2}})");
        add(R"({"name":"cap","armour":{"slot":"head","points":1}})");
        add(R"({"name":"strong_vest","armour":{"slot":"chest","points":7}})");
        add(R"({"name":"rack","placesStand":true})");
        QCOMPARE(modded.item(*QcArmour::placeholder(modded, QcEquipmentSlot::Chest)).name, u"m:weak_vest"_s);
        QCOMPARE(modded.item(*QcArmour::placeholder(modded, QcEquipmentSlot::Head)).name, u"m:cap"_s);
        QVERIFY(!QcArmour::placeholder(modded, QcEquipmentSlot::Feet));
        QCOMPARE(modded.item(*QcArmour::pointsIcon(modded)).name, u"m:strong_vest"_s);
        QCOMPARE(QcStandProperties::standItem(modded), u"m:rack"_s);

        // No armour and no stand item at all.
        const QcContentRegistry empty;
        QVERIFY(!QcArmour::pointsIcon(empty));
        QVERIFY(QcStandProperties::standItem(empty).isEmpty());
    }

    void invalidArmourIsRefused_data()
    {
        QTest::addColumn<QByteArray>("text");
        QTest::newRow("slot") << QByteArray(R"({"name":"x","armour":{"slot":"tail","points":1}})");
        QTest::newRow("no slot") << QByteArray(R"({"name":"x","armour":{"points":1}})");
        QTest::newRow("points") << QByteArray(R"({"name":"x","armour":{"slot":"head","points":21}})");
        QTest::newRow("not an object") << QByteArray(R"({"name":"x","armour":true})");
        QTest::newRow("layer texture") << QByteArray(R"({"name":"x","armour":{"slot":"head","layers":[{"covers":["head"]}]}})");
        QTest::newRow("layer covers") << QByteArray(R"({"name":"x","armour":{"slot":"head","layers":[{"covers":[],"texture":"t"}]}})");
        QTest::newRow("inflate") << QByteArray(R"({"name":"x","armour":{"slot":"head","layers":[{"covers":["head"],"texture":"t","inflate":0.5}]}})");
        QTest::newRow("heights") << QByteArray(R"({"name":"x","armour":{"slot":"feet","layers":[{"covers":["legs"],"texture":"t","fromY":0.5,"toY":0.2}]}})");
        QTest::newRow("bone") << QByteArray(R"({"name":"x","armour":{"slot":"head","parts":[{"bone":"tail","from":[0,0,0],"to":[1,1,1],"texture":"t"}]}})");
        QTest::newRow("part box") << QByteArray(R"({"name":"x","armour":{"slot":"head","parts":[{"bone":"head","from":[0,0,0],"to":[0,1,1],"texture":"t"}]}})");
        QTest::newRow("too far") << QByteArray(R"({"name":"x","armour":{"slot":"head","parts":[{"bone":"head","from":[0,0,0],"to":[9,1,1],"texture":"t"}]}})");
    }

    void invalidArmourIsRefused()
    {
        QFETCH(QByteArray, text);
        QVERIFY(!QcContentParser::parseItem(QJsonDocument::fromJson(text).object(), u"m"_s).has_value());
    }

    void baseGameHasFourSetsOfFourPieces()
    {
        QHash<QString, QList<QcEquipmentSlot>> sets;
        for (const QcItemDefinition &definition : m_content->items()) {
            if (definition.armour) {
                sets[definition.armour->set].append(definition.armour->slot);
                QVERIFY2(definition.durability > 0, qPrintable(definition.name));
                QVERIFY2(!definition.armour->layers.isEmpty(), qPrintable(definition.name));
                for (const QcArmourLayer &layer : definition.armour->layers) {
                    QVERIFY2(m_content->texture(layer.texture), qPrintable(layer.texture));
                    QVERIFY2(layer.front.isEmpty() || m_content->texture(layer.front), qPrintable(layer.front));
                }
                for (const QcArmourPart &part : definition.armour->parts) {
                    QVERIFY2(m_content->texture(part.texture), qPrintable(part.texture));
                }
            }
        }

        QCOMPARE(sets.size(), 4);
        for (const QString &set : {u"qtcraft:leather"_s, u"qtcraft:golden"_s, u"qtcraft:iron"_s, u"qtcraft:diamond"_s}) {
            QCOMPARE(sets.value(set).size(), 4);
            for (const QcEquipmentSlot slot : {QcEquipmentSlot::Head, QcEquipmentSlot::Chest, QcEquipmentSlot::Legs, QcEquipmentSlot::Feet}) {
                QVERIFY2(sets.value(set).contains(slot), qPrintable(set));
            }
        }

        // Stronger material, more points and longer life; a full diamond set reaches the cap.
        const auto total = [this](const char *set) {
            QcEquipment equipment;
            for (const char *piece : {"helmet", "chestplate", "leggings", "boots"}) {
                const QcItemId id = item(qPrintable(QString::fromLatin1(set) + u'_' + QString::fromLatin1(piece)));
                equipment.at(m_content->item(id).armour->slot) = {id, 1, 0};
            }
            return QcArmour::points(*m_content, equipment);
        };
        QCOMPARE(total("leather"), 7);
        QCOMPARE(total("golden"), 11);
        QCOMPARE(total("iron"), 15);
        QCOMPARE(total("diamond"), QcArmour::maxEffectivePoints);
        QVERIFY(m_content->item(item("leather_chestplate")).durability < m_content->item(item("iron_chestplate")).durability);
        QVERIFY(m_content->item(item("iron_chestplate")).durability < m_content->item(item("diamond_chestplate")).durability);
    }

    void armourTravelsWithTheContent()
    {
        QByteArray bytes;
        {
            QDataStream out(&bytes, QIODevice::WriteOnly);
            QcContentSerializer::write(*m_content, out);
        }
        QDataStream in(bytes);
        const QcResult<QcContentRegistry> copy = QcContentSerializer::read(in);
        QVERIFY2(copy.has_value(), qcErrorText(copy).constData());
        for (const char *name : {"iron_helmet", "diamond_helmet", "golden_boots", "leather_leggings", "armor_stand"}) {
            const QcItemDefinition &original = m_content->item(item(name));
            const QcItemDefinition &received = copy->item(*copy->itemId(original.name));
            QCOMPARE(received.armour, original.armour);
            QCOMPARE(received.placesStand, original.placesStand);
        }
    }

    void corruptArmourDataIsRefused()
    {
        QcArmourDefinition armour;
        armour.slot = QcEquipmentSlot::Chest;
        armour.points = 6;
        armour.layers.append({{u"torso"_s}, u"t"_s, {}, 0.03, 0.0, 2.0});
        QByteArray bytes;
        {
            QDataStream out(&bytes, QIODevice::WriteOnly);
            QcArmourSerializer::write(out, armour);
        }
        QDataStream in(bytes);
        const std::optional<std::optional<QcArmourDefinition>> read = QcArmourSerializer::read(in);
        QVERIFY(read);
        QCOMPARE(*read, std::optional(armour));

        // Points beyond the limit (written by a broken or hostile server) fail the whole read.
        armour.points = 99;
        bytes.clear();
        {
            QDataStream out(&bytes, QIODevice::WriteOnly);
            QcArmourSerializer::write(out, armour);
        }
        QDataStream bad(bytes);
        QVERIFY(!QcArmourSerializer::read(bad));
    }

    void hugeCoverCountIsRefusedBeforeReserving()
    {
        // A hostile server announces almost 2^32 covered slots and sends none; QDataStream's own
        // QStringList operator would reserve them all (qBadAlloc) and end with ReadPastEnd.
        QByteArray bytes;
        {
            QDataStream out(&bytes, QIODevice::WriteOnly);
            out << true << quint8(QcEquipmentSlot::Chest) << qint32(6) << u"iron"_s << quint32(1) << quint32(0xFFFFFFF0u);
        }
        QDataStream in(bytes);
        QVERIFY(!QcArmourSerializer::read(in));
        QCOMPARE(in.status(), QDataStream::ReadCorruptData);

        // Seventeen slots are one more than the parser allows.
        QcArmourDefinition armour;
        armour.layers.append({QStringList(QcArmourDefinition::maxCovers + 1, u"torso"_s), u"t"_s, {}, 0.03, 0.0, 2.0});
        bytes.clear();
        {
            QDataStream out(&bytes, QIODevice::WriteOnly);
            QcArmourSerializer::write(out, armour);
        }
        QDataStream tooMany(bytes);
        QVERIFY(!QcArmourSerializer::read(tooMany));
    }

    void pointsReduceDamageUpToTheCap()
    {
        QCOMPARE(QcArmour::reducedDamage(10.0, 0), 10.0);
        QCOMPARE(QcArmour::reducedDamage(10.0, 5), 8.0);
        QCOMPARE(QcArmour::reducedDamage(10.0, 15), 4.0);
        QCOMPARE(QcArmour::reducedDamage(10.0, 20), 2.0);
        // More than 20 points protect no better.
        QCOMPARE(QcArmour::reducedDamage(10.0, 35), 2.0);
        QCOMPARE(QcArmour::reducedDamage(10.0, -3), 10.0);
    }

    void hitsWearEveryPieceAndBreakIt()
    {
        QCOMPARE(QcArmour::wearOf(1.0), 1);
        QCOMPARE(QcArmour::wearOf(7.9), 1);
        QCOMPARE(QcArmour::wearOf(8.0), 2);
        QCOMPARE(QcArmour::wearOf(20.0), 5);

        const QcItemId helmet = item("leather_helmet");
        const QcItemId boots = item("iron_boots");
        const int helmetDurability = m_content->item(helmet).durability;
        QcEquipment worn;
        worn.at(QcEquipmentSlot::Head) = {helmet, 1, helmetDurability - 2};
        worn.at(QcEquipmentSlot::Feet) = {boots, 1, 0};

        QVERIFY(worn.wear(*m_content, 4.0).isEmpty());
        QCOMPARE(worn.at(QcEquipmentSlot::Head).damage, helmetDurability - 1);
        QCOMPARE(worn.at(QcEquipmentSlot::Feet).damage, 1);

        // The next hit uses the helmet up: it is gone, the boots stay.
        QCOMPARE(worn.wear(*m_content, 4.0), QList<QcEquipmentSlot>{QcEquipmentSlot::Head});
        QVERIFY(worn.at(QcEquipmentSlot::Head).isEmpty());
        QCOMPARE(worn.at(QcEquipmentSlot::Feet).damage, 2);
        QCOMPARE(QcArmour::points(*m_content, worn), 2);
    }

    void armourFitsOnlyItsSlot()
    {
        const QcItemId helmet = item("iron_helmet");
        QVERIFY(QcArmour::fits(*m_content, {helmet, 1, 0}, QcEquipmentSlot::Head));
        QVERIFY(!QcArmour::fits(*m_content, {helmet, 1, 0}, QcEquipmentSlot::Chest));
        QVERIFY(!QcArmour::fits(*m_content, {helmet, 2, 0}, QcEquipmentSlot::Head));
        QVERIFY(!QcArmour::fits(*m_content, {item("stick"), 1, 0}, QcEquipmentSlot::Head));
        QVERIFY(!QcArmour::fits(*m_content, {}, QcEquipmentSlot::Head));
    }

    void twoWornPiecesRepairEachOther()
    {
        const QcItemId pick = item("iron_pickaxe");
        const QcItemId chest = item("iron_chestplate");
        const int durability = m_content->item(chest).durability;
        const int bonus = int(std::floor(durability * QcRepair::bonusShare));

        QList<QcItemStack> grid(4);
        grid[0] = {chest, 1, 200};
        grid[3] = {chest, 1, 150};
        // 40 + 90 left plus 5 % of 240.
        const std::optional<QcItemStack> repaired = QcRepair::result(*m_content, grid);
        QVERIFY(repaired.has_value());
        QCOMPARE(repaired->item, chest);
        QCOMPARE(repaired->count, 1);
        QCOMPARE(durability - repaired->damage, (durability - 200) + (durability - 150) + bonus);

        // Never better than new.
        grid[0].damage = 5;
        QCOMPARE(QcRepair::result(*m_content, grid)->damage, 0);
        // Tools repair the same way.
        QCOMPARE(QcRepair::result(*m_content, {{pick, 1, 100}, {pick, 1, 100}})->item, pick);

        // Not a repair: two new pieces, different items, a third item, a single piece, no durability.
        QVERIFY(!QcRepair::result(*m_content, {{chest, 1, 0}, {chest, 1, 0}}));
        QVERIFY(!QcRepair::result(*m_content, {{chest, 1, 10}, {item("iron_leggings"), 1, 10}}));
        QVERIFY(!QcRepair::result(*m_content, {{chest, 1, 10}, {chest, 1, 10}, {item("stick"), 1, 0}}));
        QVERIFY(!QcRepair::result(*m_content, {{chest, 1, 10}, {}}));
        QVERIFY(!QcRepair::result(*m_content, {{item("stick"), 1, 0}, {item("stick"), 1, 0}}));
    }

    void shiftClickPutsArmourIntoItsFreeSlot()
    {
        constexpr int helmetSlot = QcMsg::MoveInventoryItem::equipmentSlot;
        QcQuickMove::Situation situation;
        situation.freeArmourSlot = helmetSlot;
        QCOMPARE(QcQuickMove::targets(4, situation), QList<int>{helmetSlot});
        QCOMPARE(QcQuickMove::targets(20, situation), QList<int>{helmetSlot});
        // An open chest takes it instead; from the armour slot it goes back into the inventory.
        situation.containerSlots = 27;
        QCOMPARE(QcQuickMove::targets(4, situation).size(), 27);
        QCOMPARE(QcQuickMove::targets(helmetSlot, {}).size(), 36);
        QVERIFY(QcMsg::MoveInventoryItem::isEquipmentSlot(helmetSlot + 3));
        QVERIFY(!QcMsg::MoveInventoryItem::isEquipmentSlot(helmetSlot + 4));
    }

    void standPoseIsNormalised()
    {
        QCOMPARE(qcNormalizedAngle(190.0f), -170.0f);
        QCOMPARE(qcNormalizedAngle(-180.0f), 180.0f);
        QCOMPARE(qcNormalizedAngle(720.0f), 0.0f);
        QCOMPARE(qcNormalizedAngle(std::numeric_limits<float>::quiet_NaN()), 0.0f);
        QCOMPARE(qcNormalizedAngle(std::numeric_limits<float>::infinity()), 0.0f);

        QcStandPose pose;
        pose.at(QcStandPose::Part::Head) = {400.0f, std::numeric_limits<float>::infinity(), -90.0f};
        const QcStandPose normal = pose.normalized();
        QCOMPARE(normal.at(QcStandPose::Part::Head), (QcStandPose::Rotation{40.0f, 0.0f, -90.0f}));

        for (const quint8 flags : {quint8(0), quint8(1), quint8(6), quint8(15)}) {
            QCOMPARE(QcStandPose::withFlags(flags).flags(), flags);
        }
        QCOMPARE(QcStandPose().flags(), quint8(3));
        for (std::size_t i = 0; i < QcStandPose::partCount; ++i) {
            const auto part = QcStandPose::Part(i);
            QCOMPARE(QcStandPose::partFromName(QcStandPose::partName(part)), std::optional(part));
        }
        QVERIFY(!QcStandPose::partFromName(u"tail"_s));
    }

    void standPropertiesAreChecked_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<bool>("valid");
        QTest::newRow("pose") << u"pose right_arm -90 0 10"_s << true;
        QTest::newRow("pose part") << u"pose tail 1 2 3"_s << false;
        QTest::newRow("pose count") << u"pose head 1 2"_s << false;
        QTest::newRow("pose nan") << u"pose head nan 0 0"_s << false;
        QTest::newRow("pose inf") << u"pose head inf 0 0"_s << false;
        QTest::newRow("pose huge") << u"pose head 99999 0 0"_s << false;
        QTest::newRow("pose text") << u"pose head up 0 0"_s << false;
        QTest::newRow("rotation") << u"rotation 45"_s << true;
        QTest::newRow("rotation missing") << u"rotation"_s << false;
        QTest::newRow("arms") << u"arms false"_s << true;
        QTest::newRow("plate") << u"plate off"_s << true;
        QTest::newRow("small") << u"small true"_s << true;
        QTest::newRow("showname") << u"showname on"_s << true;
        QTest::newRow("flag value") << u"small maybe"_s << false;
        QTest::newRow("name") << u"name Sir Lancelot"_s << true;
        QTest::newRow("equip set") << u"equip iron"_s << true;
        QTest::newRow("equip item") << u"equip qtcraft:iron_sword"_s << true;
        QTest::newRow("equip unknown") << u"equip unobtainium"_s << false;
        QTest::newRow("cape") << u"cape green_cape"_s << true;
        QTest::newRow("cape none") << u"cape none"_s << true;
        QTest::newRow("cape hair") << u"cape qtcraft:long_hair"_s << false;
        QTest::newRow("cape unknown") << u"cape purple_cape"_s << false;
        QTest::newRow("cape missing") << u"cape"_s << false;
        QTest::newRow("clear") << u"clear"_s << true;
        QTest::newRow("reset") << u"reset"_s << true;
        QTest::newRow("unknown") << u"dance"_s << false;
        QTest::newRow("empty") << QString() << false;
    }

    void standPropertiesAreChecked()
    {
        QFETCH(QString, line);
        QFETCH(bool, valid);
        QcArmourStand stand;
        QCOMPARE(QcStandProperties::applyLine(*m_content, stand, line), valid);
    }

    void standPropertiesChangeTheStand()
    {
        QcArmourStand stand;
        const QcContentRegistry &content = *m_content;
        QVERIFY(QcStandProperties::applyLine(content, stand, u"equip diamond"_s));
        QCOMPARE(QcArmour::points(content, stand.equipment), 20);
        QVERIFY(QcStandProperties::applyLine(content, stand, u"equip iron_helmet"_s));
        QCOMPARE(stand.equipment.at(QcEquipmentSlot::Head).item, item("iron_helmet"));
        QVERIFY(QcStandProperties::applyLine(content, stand, u"equip iron_sword"_s));
        QCOMPARE(stand.held.item, item("iron_sword"));

        QVERIFY(QcStandProperties::applyLine(content, stand, u"pose left_leg 370 -10 5"_s));
        QCOMPARE(stand.pose.at(QcStandPose::Part::LeftLeg), (QcStandPose::Rotation{10.0f, -10.0f, 5.0f}));
        QVERIFY(QcStandProperties::applyLine(content, stand, u"rotation 270"_s));
        QCOMPARE(stand.yaw, -90.0f);
        QVERIFY(QcStandProperties::applyLine(content, stand, u"arms false"_s));
        QVERIFY(QcStandProperties::applyLine(content, stand, u"small true"_s));
        QVERIFY(!stand.pose.arms && stand.pose.small);

        // Names are cleaned (no control characters) and limited.
        QVERIFY(QcStandProperties::apply(content, stand, {u"name"_s, u"  Sir\u0007 Bob  "_s}));
        QCOMPARE(stand.name, u"Sir Bob"_s);
        QVERIFY(QcStandProperties::apply(content, stand, {u"name"_s, QString(100, u'x')}));
        QCOMPARE(stand.name.size(), QcArmourStandManager::maxNameLength);

        // Capes are appearances of the kind cape (E-239, E-240); "none" takes it off.
        QVERIFY(QcStandProperties::applyLine(content, stand, u"cape green_cape"_s));
        QCOMPARE(stand.cape, u"qtcraft:green_cape"_s);
        QVERIFY(!QcStandProperties::applyLine(content, stand, u"cape tunic"_s));
        QCOMPARE(stand.cape, u"qtcraft:green_cape"_s);
        QVERIFY(QcStandProperties::applyLine(content, stand, u"cape none"_s));
        QVERIFY(stand.cape.isEmpty());
        QVERIFY(QcStandProperties::applyLine(content, stand, u"cape qtcraft:green_cape"_s));

        // Reset straightens the pose and flags; clear takes everything off.
        QVERIFY(QcStandProperties::applyLine(content, stand, u"reset"_s));
        QCOMPARE(stand.pose, QcStandPose());
        QVERIFY(!stand.equipment.isEmpty());
        QVERIFY(QcStandProperties::applyLine(content, stand, u"clear"_s));
        QVERIFY(stand.equipment.isEmpty() && stand.held.isEmpty() && stand.cape.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestArmour)
#include "test_armour.moc"
