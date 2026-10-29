// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// How items are held, from the content (decision E-831): category defaults in held.json, the
// `hold` of items and blocks, the swing placement, their checks, the content sync and a mod that
// moves an item.

#include "clientcore/meshing/QcFigureLook.h"
#include "clientcore/meshing/QcHeldItemMesher.h"
#include "common/content/loader/QcBaseContent.h"
#include "common/content/loader/QcContentPackLoader.h"
#include "common/content/parser/QcHoldParser.h"
#include "common/content/parser/QcItemParser.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/content/registry/QcHoldSerializer.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
}

QcResult<QcItemDefinition> parseItem(const QByteArray &json)
{
    return QcItemParser::parseItem(QJsonDocument::fromJson(json).object(), u"t"_s);
}

QcEntityMesher::ItemLook lookOf(const QcContentRegistry &content, const QString &item)
{
    return QcFigureLook::item(content, content.itemId(item).value_or(qcNoItem));
}

QcVec3 uniform(double value)
{
    return {value, value, value};
}

} // namespace

class TestHoldContent : public QObject
{
    Q_OBJECT

private slots:
    void baseContentHoldsByCategory()
    {
        QcContentRegistry content;
        const QcResult<QcLoadedPack> base = QcBaseContent::loadInto(content);
        QVERIFY(base);
        QVERIFY2(base->warnings.isEmpty(), qPrintable(base->warnings.join(u'\n')));
        QVERIFY(content.finish().isEmpty());

        // Every category has a full first-person and third-person transform.
        for (const QcHoldDisplay &display : content.holdDefaults().categories) {
            QVERIFY(display.firstPerson.translate && display.firstPerson.rotate && display.firstPerson.scale);
            QVERIFY(display.thirdPerson.translate && display.thirdPerson.rotate && display.thirdPerson.scale);
        }

        const QcEntityMesher::ItemLook pickaxe = lookOf(content, u"qtcraft:iron_pickaxe"_s);
        QCOMPARE(pickaxe.grip, QcHoldGrip::Handle);
        QCOMPARE(pickaxe.firstPerson.scale, *content.holdDefaults().of(QcHoldCategory::Tool).firstPerson.scale);
        QCOMPARE(lookOf(content, u"qtcraft:bow"_s).grip, QcHoldGrip::Middle);
        QCOMPARE(lookOf(content, u"qtcraft:torch"_s).grip, QcHoldGrip::Base);
        // Doors and rails have an item texture: held as sprites, by the base.
        for (const QString &sprite : {u"qtcraft:oak_door"_s, u"qtcraft:rail"_s}) {
            const QcEntityMesher::ItemLook look = lookOf(content, sprite);
            QVERIFY2(!look.cube && look.grip == QcHoldGrip::Base, qPrintable(sprite));
        }

        const QcEntityMesher::ItemLook stone = lookOf(content, u"qtcraft:stone"_s);
        QVERIFY(stone.cube && stone.boxes.isEmpty());
        QCOMPARE(stone.firstPerson.scale, *content.holdDefaults().of(QcHoldCategory::Block).firstPerson.scale);

        // Nothing of the base game turns in the fist during the swing (decision E-881): arm and item
        // swing as one rigid body, so every swing placement is the rest placement.
        for (std::size_t category = 0; category < qcHoldCategoryCount; ++category) {
            QVERIFY(content.holdDefaults().of(static_cast<QcHoldCategory>(category)).swing.isEmpty());
        }
        QCOMPARE(pickaxe.swing.scale, pickaxe.firstPerson.scale);
        for (const QString &still : {u"qtcraft:iron_pickaxe"_s, u"qtcraft:iron_sword"_s, u"qtcraft:bow"_s, u"qtcraft:torch"_s, u"qtcraft:stone"_s,
                                     u"qtcraft:oak_door"_s}) {
            const QcEntityMesher::ItemLook look = lookOf(content, still);
            QVERIFY2(look.swing.rotate == look.firstPerson.rotate && look.swing.translate == look.firstPerson.translate, qPrintable(still));
        }
    }

    void swingPlacementIsChecked()
    {
        const QcResult<QcItemDefinition> club = parseItem(R"({"name": "club", "hold": {"swing": {"translate": [0, 0.1, 0], "rotate": [0, -90, 0]}}})");
        QVERIFY2(club, qPrintable(club ? QString() : club.error()));
        QCOMPARE(club->hold.swing.translate, QcVec3(0.0, 0.1, 0.0));
        QCOMPARE(club->hold.swing.rotate, QcVec3(0.0, -90.0, 0.0));
        QVERIFY(club->hold.firstPerson.isEmpty());
        QVERIFY(!club->hold.isEmpty());

        for (const char *bad : {R"({"name": "a", "hold": {"swing": {"rotate": [0, 400, 0]}}})", R"({"name": "a", "hold": {"swing": {"translate": 1}}})",
                                R"({"name": "a", "hold": {"swing": [0, 1, 0]}})"}) {
            QVERIFY2(!parseItem(bad), bad);
        }
        // The swing keeps the item's size: "scale" there is unknown and ignored.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"unknown key 'scale'"_s));
        const QcResult<QcItemDefinition> scaled = parseItem(R"({"name": "a", "hold": {"swing": {"scale": 1}}})");
        QVERIFY(scaled && !scaled->hold.swing.scale);

        // Layers stack per field: a swing rotation over a category's swing translation.
        const QcHoldDisplay category{{}, {}, {}, {}, {QcVec3{0.0, 0.2, 0.0}, QcVec3{1.0, 2.0, 3.0}, {}}};
        const QcHoldDisplay merged = club->hold.over(category);
        QCOMPARE(merged.swing.rotate, QcVec3(0.0, -90.0, 0.0));
        QCOMPARE(merged.swing.translate, QcVec3(0.0, 0.1, 0.0));
        const QcHoldDisplay inherited = QcHoldDisplay{}.over(category);
        QCOMPARE(inherited.swing, category.swing);

        // The defaults file takes a swing per category; a later pack overrides only what it sets.
        const QcResult<QcHoldDefaults> defaults =
            QcHoldParser::parseHoldDefaults(QJsonDocument::fromJson(R"({"categories": {"tool": {"swing": {"rotate": [0, -80, 0]}}}})").object(), {});
        QVERIFY(defaults);
        QCOMPARE(defaults->of(QcHoldCategory::Tool).swing.rotate, QcVec3(0.0, -80.0, 0.0));
        QCOMPARE(QcHoldDefaults{}.merged(*defaults).of(QcHoldCategory::Tool).swing.rotate, QcVec3(0.0, -80.0, 0.0));
    }

    void swingPlacementTravelsOnTheWire()
    {
        const QcHoldDisplay display{QcHoldCategory::Weapon, QcHoldGrip::Handle, {{}, QcVec3{0.0, -20.0, 60.0}, QcVec3{0.4, 0.4, 0.4}}, {},
                                    {QcVec3{0.0, 0.05, 0.0}, QcVec3{0.0, -85.0, -15.0}, {}}};
        QByteArray bytes;
        QDataStream out(&bytes, QIODevice::WriteOnly);
        QcHoldSerializer::write(out, display);
        QDataStream in(bytes);
        const std::optional<QcHoldDisplay> read = QcHoldSerializer::read(in);
        QVERIFY(read);
        QCOMPARE(*read, display);

        // A swing out of range is refused like any other placement.
        QcHoldDisplay wild = display;
        wild.swing.rotate = QcVec3{0.0, 999.0, 0.0};
        QByteArray wildBytes;
        QDataStream wildOut(&wildBytes, QIODevice::WriteOnly);
        QcHoldSerializer::write(wildOut, wild);
        QDataStream wildIn(wildBytes);
        QVERIFY(!QcHoldSerializer::read(wildIn));
    }

    void holdObjectsAreChecked()
    {
        const QcResult<QcItemDefinition> wand = parseItem(R"({"name": "wand", "hold": {"category": "tool", "grip": "middle",
            "firstPerson": {"translate": [0.1, 0, -0.1], "rotate": [0, 90, 10], "scale": [0.2, 0.3, 0.2]}}})");
        QVERIFY2(wand, qPrintable(wand ? QString() : wand.error()));
        QCOMPARE(wand->hold.category, QcHoldCategory::Tool);
        QCOMPARE(wand->hold.grip, QcHoldGrip::Middle);
        QCOMPARE(wand->hold.firstPerson.translate, QcVec3(0.1, 0.0, -0.1));
        QCOMPARE(wand->hold.firstPerson.scale, QcVec3(0.2, 0.3, 0.2));
        QVERIFY(wand->hold.thirdPerson.isEmpty());

        // Out of range, of the wrong type or unknown names refuse the item.
        for (const char *bad : {R"({"name": "a", "hold": {"firstPerson": {"scale": 5}}})", R"({"name": "a", "hold": {"firstPerson": {"rotate": 3}}})",
                                R"({"name": "a", "hold": {"firstPerson": {"translate": [0, 9, 0]}}})", R"({"name": "a", "hold": {"grip": "teeth"}})",
                                R"({"name": "a", "hold": {"category": "hat"}})", R"({"name": "a", "hold": {"thirdPerson": []}})"}) {
            QVERIFY2(!parseItem(bad), bad);
        }

        // Unknown keys are only warned about.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"unknown key 'size'"_s));
        QVERIFY(parseItem(R"({"name": "a", "hold": {"firstPerson": {"size": 1}}})"));
    }

    void holdTravelsWithTheContent()
    {
        QcContentRegistry content;
        QVERIFY(QcBaseContent::loadInto(content));
        QVERIFY(content.finish().isEmpty());

        QByteArray bytes;
        QBuffer buffer(&bytes);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QDataStream out(&buffer);
        content.writeTo(out);
        buffer.close();
        QVERIFY(buffer.open(QIODevice::ReadOnly));
        QDataStream in(&buffer);
        const QcResult<QcContentRegistry> received = QcContentRegistry::readFrom(in);
        QVERIFY2(received, qPrintable(received ? QString() : received.error()));
        QCOMPARE(received->holdDefaults(), content.holdDefaults());
        QCOMPARE(lookOf(*received, u"qtcraft:iron_pickaxe"_s).firstPerson.rotate, lookOf(content, u"qtcraft:iron_pickaxe"_s).firstPerson.rotate);
        QCOMPARE(lookOf(*received, u"qtcraft:iron_pickaxe"_s).swing.rotate, lookOf(content, u"qtcraft:iron_pickaxe"_s).swing.rotate);
    }

    void modsMoveHeldItems()
    {
        QTemporaryDir root;
        const QString pack = QDir(root.path()).filePath(u"holdmod"_s);
        writeFile(QDir(pack).filePath(u"pack.json"_s), R"({"id": "holdmod", "version": "1.0.0"})");
        // The tool category gets a smaller first-person size; everything else stays.
        writeFile(QDir(pack).filePath(u"held.json"_s), R"({"categories": {"tool": {"firstPerson": {"scale": 0.3}}}})");
        writeFile(QDir(pack).filePath(u"items/wand.json"_s), R"({"name": "wand", "hold": {"category": "tool", "firstPerson": {"rotate": [0, 0, 10]}}})");

        QcContentRegistry content;
        QVERIFY(QcBaseContent::loadInto(content));
        const QcHoldDisplay baseTool = content.holdDefaults().of(QcHoldCategory::Tool);
        const QcResult<QcLoadedPack> mod = QcContentPackLoader::load(pack, content);
        QVERIFY2(mod && mod->warnings.isEmpty(), qPrintable(mod ? mod->warnings.join(u'\n') : mod.error()));
        QVERIFY(content.finish().isEmpty());

        const QcEntityMesher::ItemLook pickaxe = lookOf(content, u"qtcraft:iron_pickaxe"_s);
        QCOMPARE(pickaxe.firstPerson.scale, uniform(0.3));
        QCOMPARE(pickaxe.firstPerson.rotate, *baseTool.firstPerson.rotate);
        QCOMPARE(pickaxe.thirdPerson.scale, *baseTool.thirdPerson.scale);

        const QcEntityMesher::ItemLook wand = lookOf(content, u"holdmod:wand"_s);
        QCOMPARE(wand.grip, QcHoldGrip::Handle);
        QCOMPARE(wand.firstPerson.rotate, QcVec3(0.0, 0.0, 10.0));
        QCOMPARE(wand.firstPerson.scale, uniform(0.3));

        // The mesher draws the flat wand as large as its placement: 0.3 m across, upright.
        QcEntityMesh mesh;
        const QcEntityMesher::HeldItemPainter painter(mesh, [](const QString &) { return QRectF(0.0, 0.0, 1.0, 1.0); }, QcEntityLight(1.0f, 0.0f));
        QcEntityMesher::ItemLook upright = wand;
        upright.firstPerson.rotate = {};
        painter.paint(upright, upright.firstPerson, [](const QcVec3 &p) { return p; });
        QVERIFY(!mesh.vertices.isEmpty());
        float low = 1e9f;
        float high = -1e9f;
        for (const QcEntityVertex &vertex : std::as_const(mesh.vertices)) {
            low = std::min(low, vertex.y);
            high = std::max(high, vertex.y);
        }
        QVERIFY2(std::abs(double(high - low) - 0.3) < 0.01, qPrintable(QString::number(high - low)));
    }
};

QTEST_GUILESS_MAIN(TestHoldContent)
#include "test_hold_content.moc"
