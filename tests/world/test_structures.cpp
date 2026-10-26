#include "common/content/parser/QcContentParser.h"
#include "QcTestUtil.h"
#include "worldgen/structures/QcStateRotation.h"
#include "worldgen/structures/QcStructure.h"
#include "worldgen/structures/QcStructurePlacement.h"
#include "worldgen/villages/QcPieceVariation.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

QString text(const char *raw)
{
    return QString::fromUtf8(raw);
}

QJsonObject json(const char *text)
{
    return QJsonDocument::fromJson(QByteArray(text)).object();
}

QcBlockId id(const QcContentRegistry &content, const QString &state)
{
    const std::optional<QcBlockId> found = content.stateId(state);
    if (!found) {
        QcTestUtil::failCase(QStringLiteral("unknown state %1").arg(state));
        return qcAirBlockId;
    }
    return *found;
}

// An L-shaped structure: stone at (0,0,0), (1,0,0) and (0,0,1); air at (0,1,0) - every turn moves
// the cells somewhere else.
QcStructure lShape(const QcContentRegistry &content)
{
    QcStructure structure(u"test:l"_s, 2, 2, 2);
    const QcBlockId stone = id(content, u"qtcraft:stone"_s);
    structure.set(0, 0, 0, stone);
    structure.set(1, 0, 0, stone);
    structure.set(0, 0, 1, id(content, u"qtcraft:dirt"_s));
    structure.set(0, 1, 0, qcAirBlockId);
    return structure;
}

// A one-layer structure from rows of palette keys (north row first), keys mapped to `palette` by
// their position in `keys`; '.' keeps what is there.
QcStructureDefinition layer(const QStringList &rows, const QString &keys, const QStringList &palette)
{
    QcStructureDefinition definition;
    definition.name = u"test:layer"_s;
    definition.sizeX = static_cast<int>(rows.value(0).size());
    definition.sizeY = 1;
    definition.sizeZ = static_cast<int>(rows.size());
    definition.palette = palette;
    for (const QString &row : rows) {
        for (const QChar key : row) {
            definition.cells.append(static_cast<qint16>(keys.indexOf(key)));
        }
    }
    return definition;
}

// The sides ("<state> x,y,z east") of the connecting block at `pos` that face a block of a group it
// connects to inside the structure but are not connected.
QStringList unconnectedSidesOf(const QcContentRegistry &content, const QcStructure &structure, QcBlockPos pos)
{
    static constexpr std::array<std::array<int, 2>, 4> steps{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}}};
    static const std::array<QString, 4> sides{u"north"_s, u"east"_s, u"south"_s, u"west"_s};
    const QcBlockId state = *structure.block(pos.x, pos.y, pos.z);
    const QcBlockDefinition &block = content.block(state);
    QStringList missing;
    for (std::size_t side = 0; side < sides.size(); ++side) {
        const std::optional<QcBlockId> neighbour = structure.block(pos.x + steps[side][0], pos.y, pos.z + steps[side][1]);
        const bool joins = neighbour && *neighbour != qcAirBlockId && block.connectsTo.contains(content.block(*neighbour).connectGroup);
        if (joins && content.property(state, sides[side]) != u"true"_s) {
            missing.append(u"%1 %2,%3,%4 %5"_s.arg(content.stateName(state)).arg(pos.x).arg(pos.y).arg(pos.z).arg(sides[side]));
        }
    }
    return missing;
}

// The unconnected sides (unconnectedSidesOf) of all connecting blocks in the structure; cells whose
// palette entry sets a side itself are skipped.
QStringList unconnectedSides(const QcContentRegistry &content, const QcStructure &structure, const QcStructureDefinition &definition)
{
    static const QRegularExpression explicitSide(u"[\\[,](north|east|south|west|up)="_s);
    QStringList missing;
    for (int y = 0; y < structure.sizeY(); ++y) {
        for (int z = 0; z < structure.sizeZ(); ++z) {
            for (int x = 0; x < structure.sizeX(); ++x) {
                const qint16 entry = definition.cells.value(definition.cellIndex(x, y, z), QcStructureDefinition::keep);
                const std::optional<QcBlockId> state = structure.block(x, y, z);
                const bool loose = entry >= 0 && state && !definition.palette[entry].contains(explicitSide);
                if (loose && !content.block(*state).connectsTo.isEmpty()) {
                    missing.append(unconnectedSidesOf(content, structure, QcBlockPos{x, y, z}));
                }
            }
        }
    }
    return missing;
}

} // namespace

class TestStructures : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content;

private slots:
    void initTestCase() { m_content = QcTestUtil::loadContent(); }

    void parsesLayersPaletteAndExtras()
    {
        const QcResult<QcStructureDefinition> parsed = QcContentParser::parseStructure(json("{ \"name\": \"hut\", \"showcase\": { \"order\": 5 }, \"palette\": { \"#\": \"stone\", \"T\": \"torch[attach=north]\", \".\": \"air\" }, \"layers\": [ [\"###\", \"#.#\"], [\"T  \", \"  #\"] ], \"mobs\": [ { \"mob\": \"pig\", \"at\": [1.5, 1, 0.5] } ], \"contents\": [ { \"at\": [2, 1, 1], \"items\": [ { \"item\": \"coal\", \"count\": 3 }, { \"item\": \"stick\" } ] } ], \"labels\": [ { \"at\": [1, 3.5, 1], \"text\": \"block.qtcraft.stone\", \"detail\": \"x\", \"heading\": true } ], \"updates\": [ [0, 0, 0] ], \"growing\": [ [1, 1, 1] ] }"), u"qtcraft"_s);
        QVERIFY2(parsed, qPrintable(parsed ? QString() : parsed.error()));

        const QcStructureDefinition &hut = *parsed;
        QCOMPARE(hut.name, u"qtcraft:hut"_s);
        QCOMPARE(hut.translationKey, u"structure.qtcraft.hut"_s);
        QVERIFY(hut.showcase);
        QCOMPARE(hut.showcaseOrder, 5);
        QCOMPARE(hut.sizeX, 3);
        QCOMPARE(hut.sizeY, 2);
        QCOMPARE(hut.sizeZ, 2);

        // Layers go up, rows go south, characters go east; a space keeps the cell.
        const auto at = [&hut](int x, int y, int z) {
            const qint16 index = hut.cells[hut.cellIndex(x, y, z)];
            return index < 0 ? u"keep"_s : hut.palette[index];
        };
        QCOMPARE(at(0, 0, 0), u"qtcraft:stone"_s);
        QCOMPARE(at(1, 0, 1), u"qtcraft:air"_s);
        QCOMPARE(at(0, 1, 0), u"qtcraft:torch[attach=north]"_s);
        QCOMPARE(at(1, 1, 0), u"keep"_s);
        QCOMPARE(at(2, 1, 1), u"qtcraft:stone"_s);

        QCOMPARE(hut.mobs.size(), 1);
        QCOMPARE(hut.mobs[0].mob, u"qtcraft:pig"_s);
        QCOMPARE(hut.mobs[0].at, (QcVec3{1.5, 1.0, 0.5}));
        QCOMPARE(hut.contents.size(), 1);
        QCOMPARE(hut.contents[0].items.size(), 2);
        QCOMPARE(hut.contents[0].items[0].item, u"qtcraft:coal"_s);
        QCOMPARE(hut.contents[0].items[0].count, 3);
        QCOMPARE(hut.contents[0].items[1].count, 1);
        QCOMPARE(hut.labels.size(), 1);
        QVERIFY(hut.labels[0].heading);
        QCOMPARE(hut.labels[0].detail, u"x"_s);
        QCOMPARE(hut.updates, (QList<QcBlockPos>{{0, 0, 0}}));
        QCOMPARE(hut.growing, (QList<QcBlockPos>{{1, 1, 1}}));
    }

    void rejectsBrokenStructures_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("problem");

        QTest::newRow("no name") << text("{\"palette\": {\"#\": \"stone\"}, \"layers\": [[\"#\"]]}") << u"'name' is required"_s;
        QTest::newRow("unknown key") << text("{\"name\": \"a\", \"palette\": {\"#\": \"stone\"}, \"layers\": [[\"#x\"]]}") << u"does not define"_s;
        QTest::newRow("uneven rows") << text("{\"name\": \"a\", \"palette\": {\"#\": \"stone\"}, \"layers\": [[\"##\", \"#\"]]}") << u"equal length"_s;
        QTest::newRow("uneven layers") << text("{\"name\": \"a\", \"palette\": {\"#\": \"stone\"}, \"layers\": [[\"##\"], [\"##\", \"##\"]]}")
                                       << u"equally many rows"_s;
        QTest::newRow("space key") << text("{\"name\": \"a\", \"palette\": {\" \": \"stone\"}, \"layers\": [[\" \"]]}") << u"single character"_s;
        QTest::newRow("long key") << text("{\"name\": \"a\", \"palette\": {\"ab\": \"stone\"}, \"layers\": [[\"a\"]]}") << u"single character"_s;
        QTest::newRow("bad state") << text("{\"name\": \"a\", \"palette\": {\"#\": \"stone[x=1\"}, \"layers\": [[\"#\"]]}") << u"block name"_s;
        QTest::newRow("update outside") << text("{\"name\": \"a\", \"palette\": {\"#\": \"stone\"}, \"layers\": [[\"#\"]], \"updates\": [[1, 0, 0]]}")
                                        << u"cells inside"_s;
        QTest::newRow("fractional cell") << text("{\"name\": \"a\", \"palette\": {\"#\": \"stone\"}, \"layers\": [[\"#\"]], \"growing\": [[0.5, 0, 0]]}")
                                         << u"cells inside"_s;
        QTest::newRow("label far away") << text("{\"name\": \"a\", \"palette\": {\"#\": \"stone\"}, \"layers\": [[\"#\"]], \"labels\": [{\"at\": [0, 9, 0], \"text\": \"x\"}]}")
                                        << u"'labels' entries"_s;
        QTest::newRow("count too high") << text("{\"name\": \"a\", \"palette\": {\"#\": \"chest\"}, \"layers\": [[\"#\"]], \"contents\": [{\"at\": [0, 0, 0], \"items\": [{\"item\": \"coal\", \"count\": 65}]}]}")
                                        << u"count"_s;
        QTest::newRow("part outside") << text("{\"name\": \"a\", \"palette\": {\"#\": \"stone\"}, \"layers\": [[\"#\"]], \"parts\": [{\"at\": [0, 0, 0], \"layers\": [[\"##\"]]}]}")
                                      << u"outside the structure"_s;
        QTest::newRow("part without at") << text("{\"name\": \"a\", \"palette\": {\"#\": \"stone\"}, \"layers\": [[\"#\"]], \"parts\": [{\"layers\": [[\"#\"]]}]}")
                                         << u"'at'"_s;
        QTest::newRow("empty choice") << text("{\"name\": \"a\", \"palette\": {\"#\": \"stone\"}, \"layers\": [[\"#\"]], \"choices\": {\"x\": []}}")
                                      << u"1 to 16 options"_s;
        QTest::newRow("bad option") << text("{\"name\": \"a\", \"palette\": {\"#\": \"stone\"}, \"layers\": [[\"#\"]], \"choices\": {\"x\": [{\"stone\": 3}]}}")
                                    << u"name -> name"_s;
        // A frame's "map" (decision E-811): [east, south] of 0 to 3, only with a held map.
        const QString frame = text("{\"name\": \"a\", \"showcase\": {\"order\": 5}, \"palette\": {\"#\": \"stone\"}, \"layers\": [[\"#\"]], "
                                   "\"hangings\": [{\"at\": [0, 0, 0], \"face\": \"north\", %1}]}");
        QTest::newRow("map out of range") << frame.arg(u"\"item\": \"item_frame\", \"held\": \"filled_map\", \"map\": [4, 0]"_s) << u"each 0 to 3"_s;
        QTest::newRow("map of one number") << frame.arg(u"\"item\": \"item_frame\", \"held\": \"filled_map\", \"map\": [1]"_s) << u"each 0 to 3"_s;
        QTest::newRow("map on a painting") << frame.arg(u"\"item\": \"painting\", \"motif\": \"valley\", \"map\": [0, 0]"_s) << u"not to a painting"_s;
        QTest::newRow("map in an empty frame") << frame.arg(u"\"item\": \"item_frame\", \"map\": [0, 0]"_s) << u"not to a painting"_s;
        // The ground blended around a surface structure (decision E-889): 0 to 16 blocks, and the
        // footprint with its blend fits twice into a region.
        const QString surface = text("{\"name\": \"a\", \"palette\": {\"#\": \"stone\"}, \"layers\": [[\"%1\"]], "
                                     "\"surface\": {\"biomes\": [\"plains\"], \"spacing\": 64, \"blend\": %2}}");
        QTest::newRow("blend too wide") << surface.arg(u"#"_s).arg(17) << u"'blend' must be a number within 0..16"_s;
        QTest::newRow("negative blend") << surface.arg(u"#"_s).arg(-1) << u"'blend' must be a number within 0..16"_s;
        QTest::newRow("blend beyond the region") << surface.arg(QString(17, u'#')).arg(16) << u"plus its 'blend'"_s;
        const QString wide(65, u'#');
        QTest::newRow("too wide") << text("{\"name\": \"a\", \"palette\": {\"#\": \"stone\"}, \"layers\": [[\"%1\"]]}").arg(wide) << u"at most 64"_s;
    }

    void parsesVariation()
    {
        // Choices swap names in every option, parts are cells over the structure (decision E-404).
        const QcResult<QcStructureDefinition> parsed = QcContentParser::parseStructure(
            json("{ \"name\": \"hut\", \"palette\": { \"#\": \"stone\", \"C\": \"cobblestone\" }, \"layers\": [ [\"##\"], [\"  \"] ], "
                 "\"choices\": { \"rock\": [ {}, { \"stone\": \"bricks\" } ] }, \"parts\": [ { \"name\": \"top\", \"chance\": 0.25, \"at\": [1, 1, 0], \"layers\": [[\"C\"]] } ] }"),
            u"qtcraft"_s);
        QVERIFY2(parsed, qPrintable(parsed.error()));
        const QcStructureDefinition &hut = *parsed;
        QCOMPARE(hut.choices.size(), 1);
        QCOMPARE(hut.choices[0].name, u"rock"_s);
        QCOMPARE(hut.choices[0].options.size(), 2);
        QVERIFY(hut.choices[0].options[0].isEmpty());
        QCOMPARE(hut.choices[0].options[1].value(u"qtcraft:stone"_s), u"qtcraft:bricks"_s);
        QCOMPARE(hut.parts.size(), 1);
        QCOMPARE(hut.parts[0].chance, 0.25);
        QCOMPARE(hut.parts[0].cells[hut.cellIndex(1, 1, 0)], qint16(1));
        QCOMPARE(hut.parts[0].cells[hut.cellIndex(0, 1, 0)], QcStructureDefinition::keep);
        QCOMPARE(hut.cells[hut.cellIndex(1, 1, 0)], QcStructureDefinition::keep);
    }

    void parsesVillagePieceRules()
    {
        // A kind with several blueprints and rules (decision E-404); a single "structure" still works.
        const QcResult<QcVillageStyleDefinition> parsed = QcContentParser::parseVillageStyle(
            json("{ \"name\": \"s\", \"centre\": \"well\", \"pieces\": [ { \"structures\": [ { \"structure\": \"a\", \"weight\": 3 }, { \"structure\": \"b\" } ], \"weight\": 2 }, "
                 "{ \"structure\": \"hut\", \"max\": 1, \"chance\": 0.5, \"minRoadLength\": 120, \"water\": 10, \"jetty\": { \"deck\": \"spruce_planks\", \"width\": 2, \"over\": 3 } } ] }"),
            u"mod"_s);
        QVERIFY2(parsed, qPrintable(parsed.error()));
        QCOMPARE(parsed->pieces.size(), 2);
        const QcVillagePiece &houses = parsed->pieces[0];
        QCOMPARE(houses.blueprints.size(), 2);
        QCOMPARE(houses.blueprints[0].structure, u"mod:a"_s);
        QCOMPARE(houses.blueprints[0].weight, 3);
        QCOMPARE(houses.blueprints[1].weight, 1);
        QVERIFY(!houses.hasRules());
        const QcVillagePiece &hut = parsed->pieces[1];
        QCOMPARE(hut.blueprints.size(), 1);
        QCOMPARE(hut.chance, 0.5);
        QCOMPARE(hut.minRoadLength, 120);
        QCOMPARE(hut.water, 10);
        QCOMPARE(hut.jetty.deck, u"mod:spruce_planks"_s);
        // Unnamed blocks default to the base game's oak; widths are odd.
        QCOMPARE(hut.jetty.post, u"qtcraft:oak_log"_s);
        QCOMPARE(hut.jetty.width, 3);
        QCOMPARE(hut.jetty.over, 3);
        QVERIFY(hut.hasRules());

        const QcResult<QcVillageStyleDefinition> broken = QcContentParser::parseVillageStyle(
            json("{ \"name\": \"s\", \"centre\": \"well\", \"pieces\": [ { \"structures\": [] } ] }"), u"mod"_s);
        QVERIFY(!broken);
    }

    void parsesVillageLayouts()
    {
        // Street plans with their parameters over the kind's defaults (decision E-408).
        const QcResult<QcVillageStyleDefinition> parsed = QcContentParser::parseVillageStyle(
            json("{ \"name\": \"s\", \"centre\": \"well\", \"pieces\": [ { \"structure\": \"a\" } ], \"layouts\": [ "
                 "{ \"kind\": \"street\", \"weight\": 3, \"winding\": 0.5, \"branches\": { \"every\": 12, \"length\": [0.2, 0.4] } }, "
                 "{ \"kind\": \"tree\", \"arms\": [3, 4], \"bends\": 1, \"diagonals\": false }, { \"kind\": \"plaza\", \"ring\": [10, 12] } ] }"),
            u"mod"_s);
        QVERIFY2(parsed, qPrintable(parsed.error()));
        QCOMPARE(parsed->layouts.size(), 3);
        const QcVillageLayout &street = parsed->layouts[0];
        QCOMPARE(street.kind, QcVillageLayout::Kind::Street);
        QCOMPARE(street.weight, 3);
        QCOMPARE(street.winding, 0.5);
        QCOMPARE(street.branchEvery, 12);
        QCOMPARE(street.branchLength.min, 0.2);
        QCOMPARE(street.branchLength.max, 0.4);
        // The street's own defaults where the entry says nothing.
        QCOMPARE(street.armLength.min, 0.8);
        QCOMPARE(street.branchChance, 0.8);
        const QcVillageLayout &tree = parsed->layouts[1];
        QCOMPARE(tree.arms.min, 3);
        QCOMPARE(tree.arms.max, 4);
        QCOMPARE(tree.bends.min, 1);
        QCOMPARE(tree.bends.max, 1);
        QVERIFY(!tree.diagonals);
        QCOMPARE(parsed->layouts[2].ring.min, 10);

        // Without layouts the default mix of all six kinds.
        const QcResult<QcVillageStyleDefinition> plain =
            QcContentParser::parseVillageStyle(json("{ \"name\": \"s\", \"centre\": \"well\", \"pieces\": [ { \"structure\": \"a\" } ] }"), u"mod"_s);
        QVERIFY(plain);
        QCOMPARE(plain->layouts.size(), 6);

        for (const char *layouts : {"[]", "[ { \"kind\": \"star\" } ]", "[ { \"kind\": \"tree\", \"arms\": [4, 2] } ]",
                                    "[ { \"kind\": \"tree\", \"arms\": [2, 9] } ]", "[ { \"kind\": \"cross\", \"winding\": 2 } ]", "{}"}) {
            const QString text = u"{ \"name\": \"s\", \"centre\": \"well\", \"pieces\": [ { \"structure\": \"a\" } ], \"layouts\": %1 }"_s.arg(
                QLatin1StringView(layouts));
            QVERIFY2(!QcContentParser::parseVillageStyle(QJsonDocument::fromJson(text.toUtf8()).object(), u"mod"_s), layouts);
        }
    }

    void rejectsBrokenStructures()
    {
        QFETCH(QString, text);
        QFETCH(QString, problem);
        const QcResult<QcStructureDefinition> parsed = QcContentParser::parseStructure(QJsonDocument::fromJson(text.toUtf8()).object(), u"qtcraft"_s);
        QVERIFY(!parsed);
        QVERIFY2(parsed.error().contains(problem), qPrintable(parsed.error()));
    }

    void readsTheMapOfAFrame()
    {
        const QcResult<QcStructureDefinition> parsed = QcContentParser::parseStructure(
            QJsonDocument::fromJson("{\"name\": \"a\", \"showcase\": {\"order\": 5}, \"palette\": {\"#\": \"stone\"}, \"layers\": [[\"#\"]], "
                                    "\"hangings\": [{\"at\": [0, 0, 0], \"face\": \"north\", \"item\": \"item_frame\", \"held\": \"filled_map\", \"map\": [1, 3]}, "
                                    "{\"at\": [0, 0, 0], \"face\": \"south\", \"item\": \"item_frame\", \"held\": \"apple\"}]}")
                .object(),
            u"qtcraft"_s);
        QVERIFY(parsed);
        QCOMPARE(parsed->hangings.size(), qsizetype(2));
        QCOMPARE(parsed->hangings.at(0).map, std::optional(QPoint(1, 3)));
        QCOMPARE(parsed->hangings.at(0).held, u"qtcraft:filled_map"_s);
        QCOMPARE(parsed->hangings.at(1).map, std::optional<QPoint>());
    }

    void resolvesAgainstTheRegistry()
    {
        QcStructureDefinition definition;
        definition.name = u"test:x"_s;
        definition.sizeX = definition.sizeY = definition.sizeZ = 1;
        definition.palette = {u"qtcraft:oak_stairs[facing=west,half=top]"_s};
        definition.cells = {0};
        const QcStructure::Loaded structure = QcStructure::fromDefinition(definition, *m_content);
        QVERIFY(structure);
        QCOMPARE(structure->block(0, 0, 0), std::optional(id(*m_content, u"qtcraft:oak_stairs[facing=west,half=top]"_s)));

        definition.palette = {u"qtcraft:no_such_block"_s};
        QVERIFY(!QcStructure::fromDefinition(definition, *m_content));

        // Unknown properties keep the default and are reported.
        definition.palette = {u"qtcraft:oak_stairs[colour=red]"_s};
        const QcStructure::Loaded unknownProperty = QcStructure::fromDefinition(definition, *m_content);
        QVERIFY(unknownProperty);
        QCOMPARE(unknownProperty->warnings().size(), 1);

        // A failure keeps the warnings of the entries before the unknown block.
        definition.palette = {u"qtcraft:oak_stairs[colour=red]"_s, u"qtcraft:no_such_block"_s};
        const QcStructure::Loaded failed = QcStructure::fromDefinition(definition, *m_content);
        QVERIFY(!failed);
        QVERIFY(failed.error().error.contains(u"qtcraft:no_such_block"_s));
        QCOMPARE(QcStructure::warningsOf(failed), unknownProperty->warnings());
    }

    void turnsDirectionsClockwise()
    {
        QCOMPARE(QcStateRotation::turnedDirection(u"north"_s, 1), u"east"_s);
        QCOMPARE(QcStateRotation::turnedDirection(u"north"_s, 2), u"south"_s);
        QCOMPARE(QcStateRotation::turnedDirection(u"north"_s, 3), u"west"_s);
        QCOMPARE(QcStateRotation::turnedDirection(u"west"_s, 1), u"north"_s);
        QCOMPARE(QcStateRotation::turnedDirection(u"west"_s, -1), u"south"_s);
        QCOMPARE(QcStateRotation::turnedDirection(u"floor"_s, 1), u"floor"_s);
    }

    void turnsStates()
    {
        const QcContentRegistry &content = *m_content;
        // Compared as states, so the order of the properties in the names does not matter.
        const auto turned = [&content](const char *state, int turns) {
            return content.stateName(QcStateRotation::turned(content, id(content, QString::fromLatin1(state)), turns));
        };
        const auto named = [&content](const char *state) { return content.stateName(id(content, QString::fromLatin1(state))); };

        QCOMPARE(turned("qtcraft:oak_stairs[facing=north,half=top]", 1), named("qtcraft:oak_stairs[facing=east,half=top]"));
        QCOMPARE(turned("qtcraft:torch[attach=south]", 3), named("qtcraft:torch[attach=east]"));
        QCOMPARE(turned("qtcraft:torch[attach=floor]", 1), named("qtcraft:torch[attach=floor]"));
        QCOMPARE(turned("qtcraft:lantern[attach=ceiling]", 2), named("qtcraft:lantern[attach=ceiling]"));
        // Connections swap sides.
        QCOMPARE(turned("qtcraft:oak_fence[north=true,east=false,south=false,west=false]", 1),
                 named("qtcraft:oak_fence[north=false,east=true,south=false,west=false]"));
        QCOMPARE(turned("qtcraft:glass_pane[north=true,east=true,south=false,west=false]", 2),
                 named("qtcraft:glass_pane[north=false,east=false,south=true,west=true]"));
        // Door hinges stay relative to the facing.
        QCOMPARE(turned("qtcraft:oak_door[facing=south,half=bottom,open=true,hinge=right]", 1),
                 named("qtcraft:oak_door[facing=west,half=bottom,open=true,hinge=right]"));
    }

    void missingTurnedValuesAreReportedOnce()
    {
        // A mod block that can only face north or east.
        const std::shared_ptr<QcContentRegistry> content = QcTestUtil::baseContent()->registry;
        QcBlockDefinition sign;
        sign.name = u"test:half_sign"_s;
        sign.faceTextures.fill(u"qtcraft:stone"_s);
        sign.properties.append({u"facing"_s, {u"north"_s, u"east"_s}});
        QVERIFY(content->registerBlock(sign));
        const QcBlockId north = id(*content, u"test:half_sign[facing=north]"_s);

        QCOMPARE(QcStateRotation::turned(*content, north, 1), id(*content, u"test:half_sign[facing=east]"_s));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Turning test:half_sign: it has no state facing=south"_s));
        QCOMPARE(QcStateRotation::turned(*content, north, 2), north);
        QTest::failOnWarning(QRegularExpression(u"Turning test:half_sign"_s));
        QCOMPARE(QcStateRotation::turned(*content, north, 2), north);
    }

    void fourQuarterTurnsGiveTheSameState()
    {
        const QcContentRegistry &content = *m_content;
        int turnedStates = 0;
        for (QcBlockId state = 0; static_cast<qsizetype>(state) < content.stateCount(); ++state) {
            const QcBlockId once = QcStateRotation::turned(content, state, 1);
            turnedStates += once != state ? 1 : 0;
            QCOMPARE(QcStateRotation::turned(content, once, 3), state);
            QCOMPARE(QcStateRotation::turned(content, QcStateRotation::turned(content, state, 2), 2), state);
            QVERIFY(content.isSameBlock(once, state));
        }
        // Stairs, doors, fences, torches and the like all turn: hundreds of states.
        QVERIFY2(turnedStates > 300, qPrintable(QString::number(turnedStates)));
    }

    void placesEveryTurn_data()
    {
        QTest::addColumn<int>("turns");
        // Expected world offsets of the cells (0,0,0), (1,0,0) and (0,0,1) for a 2 x 2 footprint,
        // worked out on paper: a quarter turn clockwise seen from above takes north (-z) to east (+x).
        QTest::addColumn<QList<QcBlockPos>>("expected");
        QTest::newRow("0") << 0 << QList<QcBlockPos>{{0, 0, 0}, {1, 0, 0}, {0, 0, 1}};
        QTest::newRow("90") << 1 << QList<QcBlockPos>{{1, 0, 0}, {1, 0, 1}, {0, 0, 0}};
        QTest::newRow("180") << 2 << QList<QcBlockPos>{{1, 0, 1}, {0, 0, 1}, {1, 0, 0}};
        QTest::newRow("270") << 3 << QList<QcBlockPos>{{0, 0, 1}, {0, 0, 0}, {1, 0, 1}};
    }

    void placesEveryTurn()
    {
        QFETCH(int, turns);
        QFETCH(QList<QcBlockPos>, expected);
        const QcContentRegistry &content = *m_content;
        const auto structure = std::make_shared<const QcStructure>(lShape(content));
        const QcBlockPos origin{30, 70, -10};
        const QcStructurePlacement placement(structure, origin, turns, content);

        const QList<QcBlockPos> cells{{0, 0, 0}, {1, 0, 0}, {0, 0, 1}};
        for (qsizetype i = 0; i < cells.size(); ++i) {
            QCOMPARE(placement.toWorld(cells[i]), origin.offset(expected[i].x, expected[i].y, expected[i].z));
        }
        // Cell centres turn with the cells.
        const QcVec3 centre = placement.toWorld(QcVec3{1.5, 0.5, 0.5});
        QCOMPARE(centre, (QcVec3{origin.x + expected[1].x + 0.5, origin.y + 0.5, origin.z + expected[1].z + 0.5}));

        auto chunk = std::make_shared<QcChunk>(origin.chunk());
        placement.writeInto(*chunk);
        const auto blockAt = [&chunk](QcBlockPos pos) { return chunk->block(pos.localX(), pos.y, pos.localZ()); };
        QCOMPARE(blockAt(placement.toWorld(cells[0])), id(content, u"qtcraft:stone"_s));
        QCOMPARE(blockAt(placement.toWorld(cells[2])), id(content, u"qtcraft:dirt"_s));
        // The one remaining cell of the footprint is kept (air in a new chunk) and nothing spills over.
        int written = 0;
        for (int x = 0; x < QcWorld::chunkSize; ++x) {
            for (int z = 0; z < QcWorld::chunkSize; ++z) {
                written += chunk->highestNonAir(x, z) >= 0 ? 1 : 0;
            }
        }
        QCOMPARE(written, 3);
    }

    void placementAcrossChunkBordersMatchesAWholeWrite()
    {
        const QcContentRegistry &content = *m_content;
        // 5 x 3 x 4 with every cell different where possible, placed across a chunk corner.
        auto structure = std::make_shared<QcStructure>(u"test:big"_s, 5, 3, 4);
        const QList<QcBlockId> blocks{id(content, u"qtcraft:stone"_s), id(content, u"qtcraft:dirt"_s), id(content, u"qtcraft:sand"_s),
                                      id(content, u"qtcraft:oak_stairs[facing=north,half=bottom]"_s)};
        for (int y = 0; y < 3; ++y) {
            for (int z = 0; z < 4; ++z) {
                for (int x = 0; x < 5; ++x) {
                    if ((x + y + z) % 5 != 0) {
                        structure->set(x, y, z, blocks[(x + 2 * z + y) % blocks.size()]);
                    }
                }
            }
        }

        for (int turns = 0; turns < 4; ++turns) {
            const QcStructurePlacement placement(structure, {14, 64, 30}, turns, content);
            QHash<QcChunkPos, std::shared_ptr<QcChunk>> chunks;
            for (const QcChunkPos pos : {QcChunkPos{0, 1}, QcChunkPos{1, 1}, QcChunkPos{0, 2}, QcChunkPos{1, 2}}) {
                QCOMPARE(placement.overlaps(pos), pos.z == 1 || pos.z == 2);
                chunks.insert(pos, std::make_shared<QcChunk>(pos));
                placement.writeInto(*chunks[pos]);
            }

            for (int y = 0; y < 3; ++y) {
                for (int z = 0; z < 4; ++z) {
                    for (int x = 0; x < 5; ++x) {
                        const QcBlockPos world = placement.toWorld(QcBlockPos{x, y, z});
                        const QcChunk &chunk = *chunks.value(world.chunk());
                        const QcBlockId expected = placement.worldBlock({x, y, z}).value_or(qcAirBlockId);
                        QCOMPARE(chunk.block(world.localX(), world.y, world.localZ()), expected);
                    }
                }
            }
            // The stairs turn with the structure.
            QVERIFY(placement.worldBlock({3, 0, 0}) == QcStateRotation::turned(content, blocks[3], turns));
        }
    }

    void connectsFencesWallsAndPanes()
    {
        // Decision E-867: connecting blocks without connection properties in their palette entry take
        // them from their neighbours inside the structure; explicit entries stay as written.
        const QcContentRegistry &content = *m_content;
        const QcStructureDefinition definition = layer({u"FFFE"_s, u"W.SP"_s, u"WWWP"_s}, u"FEWSP"_s,
                                                       {u"qtcraft:oak_fence"_s, u"qtcraft:oak_fence[north=true]"_s, u"qtcraft:sandstone_wall"_s,
                                                        u"qtcraft:stone"_s, u"qtcraft:glass_pane"_s});
        const QcStructure::Loaded structure = QcStructure::fromDefinition(definition, content);
        QVERIFY(structure);
        const auto name = [&](int x, int z) { return content.stateName(structure->block(x, 0, z).value_or(qcAirBlockId)); };
        const auto named = [&](const char *state) { return content.stateName(id(content, QString::fromLatin1(state))); };

        // The west end of the fence row does not reach out of the structure; the explicit fence at
        // the east end keeps its own state although a fence lies west of it.
        QCOMPARE(name(0, 0), named("qtcraft:oak_fence[north=false,east=true,south=false,west=false]"));
        QCOMPARE(name(1, 0), named("qtcraft:oak_fence[north=false,east=true,south=false,west=true]"));
        // The stone south of it is solid: the fence joins it too.
        QCOMPARE(name(2, 0), named("qtcraft:oak_fence[north=false,east=true,south=true,west=true]"));
        QCOMPARE(name(3, 0), named("qtcraft:oak_fence[north=true,east=false,south=false,west=false]"));
        // A wall corner keeps its post, a straight run hides it, the stone joins the wall (solid);
        // the cell that keeps what is there counts as air.
        QCOMPARE(content.property(*structure->block(0, 0, 2), u"north"_s), u"true"_s);
        QCOMPARE(content.property(*structure->block(0, 0, 2), u"up"_s), u"true"_s);
        QCOMPARE(content.property(*structure->block(1, 0, 2), u"up"_s), u"false"_s);
        QCOMPARE(content.property(*structure->block(2, 0, 2), u"north"_s), u"true"_s);
        QCOMPARE(content.property(*structure->block(0, 0, 1), u"east"_s), u"false"_s);
        // The panes join each other and the stone, not the wall.
        QCOMPARE(name(3, 1), named("qtcraft:glass_pane[north=false,east=false,south=true,west=true]"));
        QCOMPARE(name(3, 2), named("qtcraft:glass_pane[north=true,east=false,south=false,west=false]"));
        QVERIFY2(unconnectedSides(content, *structure, definition).isEmpty(), qPrintable(unconnectedSides(content, *structure, definition).join(u'\n')));

        // Turned a quarter clockwise, the east-west fence row runs north-south.
        const auto shared = std::make_shared<const QcStructure>(*structure);
        const QcStructurePlacement placement(shared, {0, 64, 0}, 1, content);
        QCOMPARE(content.stateName(*placement.worldBlock({1, 0, 0})), named("qtcraft:oak_fence[north=true,east=false,south=true,west=false]"));
        QCOMPARE(content.stateName(*placement.worldBlock({0, 0, 0})), named("qtcraft:oak_fence[north=false,east=false,south=true,west=false]"));
    }

    void connectsTheBuiltCopyOfAVariation()
    {
        // A part fills the gap of a fence row and a choice swaps the wood: the built copy connects
        // across the filled gap with the swapped fences.
        const QcContentRegistry &content = *m_content;
        QcStructureDefinition definition = layer({u"F.F"_s}, u"F"_s, {u"qtcraft:oak_fence"_s});
        definition.choices.append({u"wood"_s, {{{u"qtcraft:oak_fence"_s, u"qtcraft:spruce_fence"_s}}}});
        definition.parts.append({u"gap"_s, 0.5, {QcStructureDefinition::keep, 0, QcStructureDefinition::keep}});
        const QcPieceVariation::Resolved variation = QcPieceVariation::resolve(definition, content);
        QVERIFY(variation);

        const std::shared_ptr<const QcStructure> built = variation->build(variation->everything());
        QCOMPARE(content.stateName(*built->block(1, 0, 0)), content.stateName(id(content, u"qtcraft:spruce_fence[north=false,east=true,south=false,west=true]"_s)));
        QCOMPARE(content.property(*built->block(0, 0, 0), u"east"_s), u"true"_s);
        // Without the part the two ends stand alone.
        const std::shared_ptr<const QcStructure> open = variation->build({{0}, {false}});
        QCOMPARE(content.property(*open->block(0, 0, 0), u"east"_s), u"false"_s);
    }

    void baseStructuresConnectTheirFences()
    {
        // Every structure of the base game: no fence, wall, pane, bar or tripwire facing a block it
        // connects to inside the structure is left unconnected (decision E-867).
        int connecting = 0;
        for (const QcStructureDefinition &definition : m_content->worldgen().structures()) {
            const QcStructure::Loaded structure = QcStructure::fromDefinition(definition, *m_content);
            QVERIFY2(structure, qPrintable(definition.name));
            const QStringList missing = unconnectedSides(*m_content, *structure, definition);
            QVERIFY2(missing.isEmpty(), qPrintable(definition.name + u": "_s + missing.join(u", "_s)));
            for (const QString &entry : definition.palette) {
                connecting += m_content->stateId(entry) && !m_content->block(*m_content->stateId(entry)).connectsTo.isEmpty() ? 1 : 0;
            }
        }
        QVERIFY2(connecting >= 20, qPrintable(QString::number(connecting)));
    }

    void baseStructuresLoad()
    {
        // Every structure of the base game resolves without warnings.
        QVERIFY(m_content->worldgen().structures().size() >= 8);
        for (const QcStructureDefinition &definition : m_content->worldgen().structures()) {
            const QcStructure::Loaded structure = QcStructure::fromDefinition(definition, *m_content);
            QVERIFY2(structure, qPrintable(structure ? QString() : structure.error().error));
            QVERIFY2(structure->warnings().isEmpty(), qPrintable(structure->warnings().join(u'\n')));
            for (const QcStructureDefinition::Label &label : definition.labels) {
                QVERIFY2(m_content->translations().has(label.text, u"en"_s), qPrintable(label.text));
            }
            QVERIFY2(m_content->translations().has(definition.translationKey, u"en"_s), qPrintable(definition.translationKey));
            QVERIFY2(m_content->translations().has(definition.translationKey, u"de"_s), qPrintable(definition.translationKey));
        }
    }
};

QTEST_GUILESS_MAIN(TestStructures)
#include "test_structures.moc"
