#include "common/content/parser/QcContentParser.h"
#include "common/content/registry/QcPartGenerator.h"
#include "common/gameplay/QcCrafting.h"
#include "common/gameplay/QcMining.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcConnection.h"
#include "common/protocol/QcInProcessTransport.h"
#include "QcRawClient.h"
#include "QcTestUtil.h"
#include "server/blocks/QcBlockRules.h"
#include "server/core/QcServer.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"

#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

QJsonObject json(const char *text)
{
    return QJsonDocument::fromJson(QByteArray(text)).object();
}

// Base content plus a test block that needs a solid block below and is one itself, so that columns
// of it fall apart one by one.
std::shared_ptr<const QcContentRegistry> buildingContent()
{
    const std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
    QcBlockDefinition stack;
    stack.name = u"test:stack"_s;
    stack.support = QcBlockSupport::Below;
    stack.faceTextures.fill(u"qtcraft:sand"_s);
    if (!content->registerBlock(stack)) {
        // Builds content for a member initialiser, outside any test case; no case can run without it.
        qFatal("cannot register test:stack");
    }

    // A storage block smaller than a chest.
    QcBlockDefinition crate;
    crate.name = u"test:crate"_s;
    crate.containerSlots = 9;
    crate.faceTextures.fill(u"qtcraft:oak_planks"_s);
    if (!content->registerBlock(crate)) {
        // Builds content for a member initialiser, outside any test case; no case can run without it.
        qFatal("cannot register test:crate");
    }
    return content;
}

} // namespace

// Building parts, block states on the server, connections, support, the update cascade and chests.
class TestBuilding : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = buildingContent();
    std::unique_ptr<QcServer> m_server;
    std::unique_ptr<QcRawClient> m_client;
    QcBlockPos m_feet;
    quint32 m_sequence = 1;

    QcBlockId id(const QString &state) const
    {
        const std::optional<QcBlockId> found = m_content->stateId(state.contains(u':') ? state : u"qtcraft:"_s + state);
        if (!found) {
            QcTestUtil::failCase(QStringLiteral("unknown block state %1").arg(state));
            return qcAirBlockId;
        }
        return *found;
    }
    QcItemId item(const char *name) const { return *m_content->itemId(u"qtcraft:"_s + QString::fromLatin1(name)); }
    QcBlockId at(QcBlockPos pos) const { return m_server->world().blockAt(pos).value_or(qcAirBlockId); }
    QString nameAt(QcBlockPos pos) const { return m_content->stateName(at(pos)); }

    void ticks(int count) { QcTestUtil::ticks(*m_server, count); }

    // Joins a server; `stage` clears a flat stone stage around the player (not for reloaded worlds).
    void join(QcServerConfig config = {}, bool stage = true)
    {
        config.viewDistance = 1;
        config.operators = {u"Builder"_s};
        // These tests build away from the player (stage 31.3: chunks would leave the world).
        config.keepChunksLoaded = true;
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_server->openWorld().has_value());

        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        m_client = std::make_unique<QcRawClient>(std::move(clientEnd));
        m_client->hello(u"Builder"_s);
        QcTestUtil::pump();
        ticks(2);

        const QcVec3 p = m_client->motion.position;
        m_feet = {int(std::floor(p.x)), int(std::floor(p.y)), int(std::floor(p.z))};
        for (int cx = -1; cx <= 1; ++cx) {
            for (int cz = -1; cz <= 1; ++cz) {
                m_server->world().loadChunk({m_feet.chunk().x + cx, m_feet.chunk().z + cz});
            }
        }

        // A flat stone stage with room above.
        for (int dx = -4; dx <= 4 && stage; ++dx) {
            for (int dz = -4; dz <= 4; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id(u"stone"_s));
                for (int dy = 0; dy <= 4; ++dy) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
    }

    void send(const QcMessage &message)
    {
        m_client->connection.send(message);
        QcTestUtil::pump();
        ticks(1);
    }

    void chat(const QString &text) { send(QcMsg::ChatInput{text}); }

    void face(float yaw)
    {
        QcPlayerInput input;
        input.sequence = ++m_sequence;
        input.yaw = yaw;
        send(QcMsg::PlayerInput{input});
    }

    // Puts `count` of an item into the selected hotbar slot.
    void hold(const char *name, int count = 1)
    {
        chat(u"/give %1 %2"_s.arg(QString::fromLatin1(name)).arg(count));
        for (int slot = 0; slot < m_client->stacks.size(); ++slot) {
            if (m_client->stacks[slot].item == item(name)) {
                send(QcMsg::SelectHotbarSlot{slot});
                return;
            }
        }
        QFAIL("item not given");
    }

    void use(QcBlockPos target, QcBlockFace face, const QcVec3 &hit = QcVec3{0.5, 0.5, 0.5}) { send(QcMsg::UseItem{target, face, hit}); }

    int dropped(QcItemId wanted) const
    {
        int total = 0;
        for (const QcItemEntity &entity : m_server->items().items()) {
            total += entity.stack.item == wanted ? entity.stack.count : 0;
        }
        return total;
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void baseMaterialsGetTheirParts()
    {
        // Woods get stairs, slabs, fences and gates; stone kinds stairs, slabs and walls, except
        // that plain and polished stone leave walls out.
        QStringList expected;
        for (const QString &wood : {u"oak"_s, u"birch"_s, u"spruce"_s}) {
            for (const QString &part : {u"stairs"_s, u"slab"_s, u"fence"_s, u"fence_gate"_s}) {
                expected.append(u"qtcraft:%1_%2"_s.arg(wood, part));
            }
        }
        for (const QString &stone : {u"cobblestone"_s, u"brick"_s, u"sandstone"_s}) {
            for (const QString &part : {u"stairs"_s, u"slab"_s, u"wall"_s}) {
                expected.append(u"qtcraft:%1_%2"_s.arg(stone, part));
            }
        }
        expected.append({u"qtcraft:stone_stairs"_s, u"qtcraft:stone_slab"_s});
        expected.append({u"qtcraft:mud_bricks_stairs"_s, u"qtcraft:mud_bricks_slab"_s, u"qtcraft:mud_bricks_wall"_s});
        expected.append({u"qtcraft:smooth_sandstone_stairs"_s, u"qtcraft:smooth_sandstone_slab"_s, u"qtcraft:smooth_sandstone_wall"_s});
        expected.append({u"qtcraft:stone_bricks_stairs"_s, u"qtcraft:stone_bricks_slab"_s, u"qtcraft:stone_bricks_wall"_s,
                         u"qtcraft:polished_stone_stairs"_s, u"qtcraft:polished_stone_slab"_s});

        // Wool of every colour gets carpets only.
        for (const QString &colour : {u"white"_s, u"red"_s, u"yellow"_s, u"blue"_s, u"green"_s, u"black"_s, u"orange"_s, u"purple"_s,
                                      u"light_gray"_s, u"gray"_s, u"brown"_s, u"lime"_s, u"cyan"_s, u"light_blue"_s, u"magenta"_s, u"pink"_s}) {
            expected.append(u"qtcraft:%1_wool_carpet"_s.arg(colour));
        }

        QStringList generated;
        for (const QcBlockDefinition &block : m_content->blocks()) {
            if (!block.partOf.isEmpty()) {
                generated.append(block.name);
            }
        }

        expected.sort();
        generated.sort();
        QCOMPARE(generated, expected);
        QVERIFY(!m_content->blockId(u"qtcraft:stone_wall"_s));

        const QcBlockDefinition &planks = m_content->block(id(u"oak_planks"_s));
        const QcBlockDefinition &stairs = m_content->block(id(u"oak_stairs"_s));
        QCOMPARE(stairs.hardness, planks.hardness);
        QCOMPARE(stairs.tool, planks.tool);
        QCOMPARE(stairs.soundMaterial, planks.soundMaterial);
        QCOMPARE(stairs.fuelSeconds, 15.0 * 0.75);
        QCOMPARE(stairs.faceTextures, planks.faceTextures);
        QCOMPARE(stairs.drops.size(), 1);
        QCOMPARE(stairs.drops.first().item, u"qtcraft:oak_stairs"_s);
        QCOMPARE(stairs.partOf, u"qtcraft:oak/qtcraft:stairs"_s);
        QCOMPARE(m_content->block(id(u"cobblestone_wall"_s)).toolLevel, m_content->block(id(u"cobblestone"_s)).toolLevel);
        QVERIFY(m_content->itemId(u"qtcraft:oak_stairs"_s));

        // Names in both languages from the family's pattern and the material's name.
        QCOMPARE(m_content->translations().translate(stairs.translationKey, u"en"_s), u"Oak stairs"_s);
        QCOMPARE(m_content->translations().translate(stairs.translationKey, u"de"_s), u"Eichentreppe"_s);
        QCOMPARE(m_content->translations().translate(m_content->block(id(u"cobblestone_wall"_s)).translationKey, u"de"_s), u"Bruchsteinmauer"_s);
        QCOMPARE(m_content->translations().translate(m_content->block(id(u"spruce_fence_gate"_s)).translationKey, u"de"_s), u"Fichtenzauntor"_s);
    }

    void excludedMaterialsMakeNothing()
    {
        // Every base block of the exclusion list, and blocks excluded by rule, have no parts.
        const QStringList suffixes = {u"stairs"_s, u"slab"_s, u"wall"_s, u"fence"_s, u"fence_gate"_s};
        for (const QString &base : {u"coal_ore"_s, u"iron_ore"_s, u"sand"_s, u"gravel"_s, u"clay"_s, u"ice"_s, u"glass"_s,
                                   u"furnace"_s, u"crafting_table"_s, u"chest"_s, u"bedrock"_s, u"water"_s, u"oak_leaves"_s,
                                   u"cactus"_s, u"oak_sapling"_s, u"dirt"_s, u"grass_block"_s}) {
            for (const QString &suffix : suffixes) {
                QVERIFY2(!m_content->blockId(u"qtcraft:%1_%2"_s.arg(base, suffix)), qPrintable(base + u'_' + suffix));
            }
        }

        for (const QcBlockDefinition &block : m_content->blocks()) {
            if (block.partOf.isEmpty()) {
                continue;
            }
            const QString material = block.partOf.section(u'/', 0, 0);
            const auto definition = std::ranges::find(m_content->materials(), material, &QcMaterialDefinition::name);
            QVERIFY(definition != m_content->materials().end());
            QVERIFY(QcPartGenerator::unsuitableBase(*m_content, definition->block).isEmpty());
        }

        QVERIFY(!QcPartGenerator::unsuitableBase(*m_content, u"qtcraft:glass"_s).isEmpty());
        QVERIFY(!QcPartGenerator::unsuitableBase(*m_content, u"qtcraft:furnace"_s).isEmpty());
        QVERIFY(!QcPartGenerator::unsuitableBase(*m_content, u"qtcraft:bedrock"_s).isEmpty());
        QVERIFY(!QcPartGenerator::unsuitableBase(*m_content, u"qtcraft:torch"_s).isEmpty());
    }

    void generatorFollowsMaterialsAndExclusions()
    {
        QcContentRegistry registry;
        for (const char *block : {R"({"name":"marble","hardness":2,"tool":"pickaxe","textures":{"all":"marble"}})",
                                  R"({"name":"basalt","textures":{"all":"basalt"}})",
                                  R"({"name":"crystal","render":"translucent","opaque":false,"textures":{"all":"crystal"}})",
                                  R"({"name":"basalt_steps","textures":{"all":"basalt"}})"}) {
            QVERIFY(registry.registerBlock(*QcContentParser::parseBlock(json(block), u"m"_s)));
        }

        registry.registerPartFamily(*QcContentParser::parsePartFamily(
            json(R"({"name":"steps","suffix":"steps","kinds":["rock"],"hardness":0.5,"block":{"shape":{"boxes":[[0,0,0,1,0.5,1]]}},)"
                 "\"recipe\":{\"pattern\":[\"##\"],\"key\":{\"#\":\"@\"},\"count\":3},\"translations\":{\"en\":\"%1 steps\"}}"),
            u"m"_s));
        registry.registerPartFamily(*QcContentParser::parsePartFamily(
            json(R"({"name":"posts","kinds":["rock"],"block":{"shape":{"boxes":[[0.25,0,0.25,0.75,1,0.75]]}},)"
                 "\"recipe\":{\"pattern\":[\"#\",\"#\"],\"key\":{\"#\":\"@\"}}}"),
            u"m"_s));

        for (const char *material : {R"({"name":"marble","block":"marble","kind":"rock","exclude":["posts"]})",
                                     R"({"name":"basalt","block":"basalt","kind":"rock"})",
                                     R"({"name":"crystal","block":"crystal","kind":"rock"})",
                                     R"({"name":"wood","block":"marble","kind":"wood"})",
                                     R"({"name":"none","excluded":true,"blocks":["basalt"]})"}) {
            registry.registerMaterial(*QcContentParser::parseMaterial(json(material), u"m"_s));
        }

        registry.translations().add(u"en"_s, {{u"material.m.marble"_s, u"Marble"_s}});
        const QStringList warnings = registry.finish();

        // Marble: steps only (posts excluded for it); basalt: nothing (excluded entry); crystal:
        // nothing (not opaque); "wood": no family of that kind.
        QVERIFY(registry.blockId(u"m:marble_steps"_s));
        QVERIFY(!registry.blockId(u"m:marble_posts"_s));
        QVERIFY(!registry.blockId(u"m:basalt_posts"_s));
        QVERIFY(!registry.blockId(u"m:crystal_steps"_s));
        QVERIFY(!registry.blockId(u"m:wood_steps"_s));

        // The explicitly defined block of a matching name stays as it was.
        QVERIFY(registry.state(*registry.blockId(u"m:basalt_steps"_s)).fullCube);
        QCOMPARE(warnings.size(), 1);
        QVERIFY2(warnings.first().contains(u"m:crystal"_s), qPrintable(warnings.first()));

        const QcBlockDefinition &steps = registry.block(*registry.blockId(u"m:marble_steps"_s));
        QCOMPARE(steps.hardness, 1.0);
        QCOMPARE(steps.tool, u"pickaxe"_s);
        QVERIFY(!registry.state(*registry.blockId(u"m:marble_steps"_s)).fullCube);
        QCOMPARE(registry.translations().translate(steps.translationKey, u"en"_s), u"Marble steps"_s);
        const std::optional<qsizetype> recipe = registry.recipeIndex(u"m:marble_steps"_s);
        QVERIFY(recipe);
        QCOMPARE(registry.recipes()[*recipe].resultCount, 3);
        QCOMPARE(QcCrafting::requirements(registry.recipes()[*recipe]), (QList<std::pair<QString, int>>{{u"m:marble"_s, 2}}));

        // Generating again (another finish) changes nothing.
        const qsizetype blocks = registry.blockCount();
        QCOMPARE(registry.finish().size(), 1);
        QCOMPARE(registry.blockCount(), blocks);
    }

    void generatedPartsRespectOtherPacks()
    {
        QcContentRegistry registry;
        for (const char *block : {R"({"name":"marble","textures":{"all":"marble"}})", R"({"name":"granite","textures":{"all":"granite"}})"}) {
            QVERIFY(registry.registerBlock(*QcContentParser::parseBlock(json(block), u"m"_s)));
        }
        registry.registerPartFamily(*QcContentParser::parsePartFamily(
            json(R"({"name":"steps","kinds":["rock"],"block":{"shape":{"boxes":[[0,0,0,1,0.5,1]]}},)"
                 "\"recipe\":{\"pattern\":[\"##\"],\"key\":{\"#\":\"@\"},\"count\":3}}"),
            u"m"_s));
        registry.registerPartFamily(*QcContentParser::parsePartFamily(
            json(R"({"name":"posts","kinds":["rock"],"block":{"shape":{"boxes":[[0.25,0,0.25,0.75,1,0.75]]}},)"
                 "\"recipe\":{\"pattern\":[\"#\",\"#\"],\"key\":{\"#\":\"@\"}}}"),
            u"m"_s));
        // A material of another pack ("o") leaves out a family of pack "m" by its full name.
        registry.registerMaterial(*QcContentParser::parseMaterial(json(R"({"name":"marble","block":"m:marble","kind":"rock","exclude":["m:posts"]})"), u"o"_s));
        registry.registerMaterial(*QcContentParser::parseMaterial(json(R"({"name":"granite","block":"m:granite","kind":"rock"})"), u"o"_s));

        // A pack's own recipe under the name of a generated part is kept.
        registry.registerRecipe(*QcContentParser::parseRecipe(
            json("{\"name\":\"granite_steps\",\"pattern\":[\"#\"],\"key\":{\"#\":\"m:marble\"},\"result\":{\"item\":\"o:granite_steps\",\"count\":7}}"),
            u"o"_s));
        registry.finish();
        QVERIFY(registry.blockId(u"o:marble_steps"_s));
        QVERIFY(!registry.blockId(u"o:marble_posts"_s));
        QVERIFY(registry.blockId(u"o:granite_posts"_s));
        const std::optional<qsizetype> recipe = registry.recipeIndex(u"o:granite_steps"_s);
        QVERIFY(recipe);
        QCOMPARE(registry.recipes()[*recipe].resultCount, 7);
        QCOMPARE(QcCrafting::requirements(registry.recipes()[*recipe]), (QList<std::pair<QString, int>>{{u"m:marble"_s, 1}}));

        // Parts without such a recipe still get the generated one.
        QCOMPARE(registry.recipes()[*registry.recipeIndex(u"o:marble_steps"_s)].resultCount, 3);
    }

    void partsAndTagsWorkInTheCraftingGrid()
    {
        const QcItemId oak = item("oak_planks");
        const QcItemId birch = item("birch_planks");
        QList<QcItemStack> grid(9);

        // Stairs from six oak planks in the stairs pattern.
        for (const int cell : {0, 3, 4, 6, 7, 8}) {
            grid[cell] = {oak, 1, 0};
        }
        std::optional<qsizetype> match = QcCrafting::match(*m_content, grid, 3);
        QVERIFY(match);
        QCOMPARE(m_content->recipes()[*match].resultItem, u"qtcraft:oak_stairs"_s);
        QCOMPARE(m_content->recipes()[*match].resultCount, 4);

        // Mixed planks are no stairs of either wood.
        grid[8] = {birch, 1, 0};
        QVERIFY(!QcCrafting::match(*m_content, grid, 3));

        // Tagged ingredients take any planks: sticks from oak over birch.
        QList<QcItemStack> small(4);
        small[0] = {oak, 1, 0};
        small[2] = {birch, 1, 0};
        match = QcCrafting::match(*m_content, small, 2);
        QVERIFY(match);
        QCOMPARE(m_content->recipes()[*match].resultItem, u"qtcraft:stick"_s);

        // Torches from charcoal or coal (tag "coals").
        small = QList<QcItemStack>(4);
        small[0] = {item("charcoal"), 1, 0};
        small[2] = {item("stick"), 1, 0};
        QCOMPARE(m_content->recipes()[*QcCrafting::match(*m_content, small, 2)].resultItem, u"qtcraft:torch"_s);

        // Removing a tagged ingredient takes from several kinds of planks.
        QcInventory inventory;
        inventory.add(oak, 2, *m_content);
        inventory.add(birch, 3, *m_content);
        QCOMPARE(QcCrafting::available(*m_content, inventory, u"#qtcraft:planks"_s), 5);
        QVERIFY(QcCrafting::removeIngredient(*m_content, inventory, u"#qtcraft:planks"_s, 4));
        QCOMPARE(QcCrafting::available(*m_content, inventory, u"#qtcraft:planks"_s), 1);
        QVERIFY(!QcCrafting::removeIngredient(*m_content, inventory, u"#qtcraft:planks"_s, 2));
    }

    void namedIngredientsGoBeforeTags()
    {
        // A recipe listing the tag before a named plank: the tag must not take the oak plank.
        QcRecipeDefinition recipe;
        recipe.name = u"test:mixed"_s;
        recipe.pattern = {u"XY"_s};
        recipe.keys = {{u'X', u"#qtcraft:planks"_s}, {u'Y', u"qtcraft:oak_planks"_s}};
        recipe.resultItem = u"qtcraft:stick"_s;
        QCOMPARE(QcCrafting::requirements(recipe),
                 (QList<std::pair<QString, int>>{{u"qtcraft:oak_planks"_s, 1}, {u"#qtcraft:planks"_s, 1}}));
        QcInventory inventory;
        inventory.add(item("oak_planks"), 1, *m_content);
        inventory.add(item("birch_planks"), 1, *m_content);
        QCOMPARE(QcCrafting::timesCraftable(*m_content, inventory, recipe, 64), 1);
        QcInventory attempt = inventory;
        QVERIFY(QcCrafting::consume(*m_content, attempt, recipe, 1));
        QCOMPARE(attempt.countOf(item("oak_planks")) + attempt.countOf(item("birch_planks")), 0);

        // Two oak planks fit each ingredient on its own twice, but make the recipe only once.
        QcInventory oak;
        oak.add(item("oak_planks"), 2, *m_content);
        QCOMPARE(QcCrafting::available(*m_content, oak, u"#qtcraft:planks"_s), 2);
        QCOMPARE(QcCrafting::timesCraftable(*m_content, oak, recipe, 64), 1);

        // Three oak and two birch: oak for Y twice, then birch for the tag twice, then one oak left.
        QcInventory mixed;
        mixed.add(item("oak_planks"), 3, *m_content);
        mixed.add(item("birch_planks"), 2, *m_content);
        QCOMPARE(QcCrafting::timesCraftable(*m_content, mixed, recipe, 64), 2);
        QCOMPARE(QcCrafting::timesCraftable(*m_content, mixed, recipe, 1), 1);
    }

    void placementDerivesStatesFromViewAndFace()
    {
        join();
        const QcBlockPos floor = m_feet.offset(2, -1, 0);
        // Looking east (yaw 90): stairs climb east, furnaces and chests face back west.
        face(90.0f);
        hold("oak_stairs", 4);
        use(floor, QcBlockFace::Up);
        QCOMPARE(nameAt(floor.offset(0, 1, 0)), u"qtcraft:oak_stairs[facing=east,half=bottom]"_s);

        // Against the upper half of a side face: upside down.
        use(floor.offset(0, 1, 0), QcBlockFace::South, {0.5, 0.8, 1.0});
        QCOMPARE(nameAt(floor.offset(0, 1, 1)), u"qtcraft:oak_stairs[facing=east,half=top]"_s);
        hold("furnace");
        use(floor.offset(0, 0, -1), QcBlockFace::Up);
        QCOMPARE(nameAt(floor.offset(0, 1, -1)), u"qtcraft:furnace[facing=west]"_s);

        // Slabs: bottom on a floor, doubled by clicking their top.
        face(0.0f);
        hold("brick_slab", 4);
        const QcBlockPos slab = m_feet.offset(-2, 0, 0);
        use(slab.offset(0, -1, 0), QcBlockFace::Up);
        QCOMPARE(nameAt(slab), u"qtcraft:brick_slab[type=bottom]"_s);
        use(slab, QcBlockFace::Up);
        QCOMPARE(nameAt(slab), u"qtcraft:brick_slab[type=double]"_s);
        QCOMPARE(m_client->stacks[m_client->stacks.indexOf(QcItemStack{item("brick_slab"), 2, 0})].count, 2);
        chat(u"/setblock %1 %2 %3 air"_s.arg(slab.x).arg(slab.y).arg(slab.z));
        QCOMPARE(at(slab), qcAirBlockId);

        // Torches: on the floor, on a wall (attached towards the clicked block), never on a ceiling.
        hold("torch", 4);
        const QcBlockPos wall = m_feet.offset(0, 0, -3);
        m_server->world().setBlock(wall, id(u"stone"_s));
        use(wall.offset(0, -1, 1), QcBlockFace::Up);
        QCOMPARE(nameAt(wall.offset(0, 0, 1)), u"qtcraft:torch[attach=floor]"_s);
        use(wall, QcBlockFace::East);
        QCOMPARE(nameAt(wall.offset(1, 0, 0)), u"qtcraft:torch[attach=west]"_s);
        m_server->world().setBlock(m_feet.offset(3, 2, 3), id(u"stone"_s));
        use(m_feet.offset(3, 2, 3), QcBlockFace::Down);
        QCOMPARE(at(m_feet.offset(3, 1, 3)), qcAirBlockId);
    }

    void doorsAreTwoBlocksThatOpenAndBreakTogether()
    {
        join();
        face(0.0f);
        hold("oak_door", 2);
        const QcBlockPos door = m_feet.offset(0, 0, -2);

        // Aiming at the west (left) half of the floor block: the hinge is on the left.
        use(door.offset(0, -1, 0), QcBlockFace::Up, {0.2, 1.0, 0.5});
        QCOMPARE(nameAt(door), u"qtcraft:oak_door[facing=north,half=bottom,hinge=left,open=false]"_s);
        QCOMPARE(nameAt(door.offset(0, 1, 0)), u"qtcraft:oak_door[facing=north,half=top,hinge=left,open=false]"_s);

        // Using either half opens both; a closed door lies on the south edge, an open one on the hinge side.
        use(door.offset(0, 1, 0), QcBlockFace::South);
        QCOMPARE(m_content->property(at(door), u"open"_s), u"true"_s);
        QCOMPARE(m_content->property(at(door.offset(0, 1, 0)), u"open"_s), u"true"_s);
        QCOMPARE(m_content->state(at(door)).boxes, (QList<QcAabb>{{{0, 0, 0}, {0.1875, 1, 1}}}));

        // A door needs two free blocks.
        const QcBlockPos low = m_feet.offset(2, 0, -2);
        m_server->world().setBlock(low.offset(0, 1, 0), id(u"stone"_s));
        use(low.offset(0, -1, 0), QcBlockFace::Up);
        QCOMPARE(at(low), qcAirBlockId);

        // Mining the upper half takes the lower one with it, and one door drops.
        send(QcMsg::StartMining{door.offset(0, 1, 0)});
        ticks(int(std::ceil(QcMining::breakSeconds(m_content->block(at(door)), nullptr) * QcPhysics::tickRate)));
        send(QcMsg::FinishMining{door.offset(0, 1, 0)});
        QCOMPARE(at(door.offset(0, 1, 0)), qcAirBlockId);
        QCOMPARE(at(door), qcAirBlockId);
        QCOMPARE(dropped(item("oak_door")), 1);
        ticks(2);
        QCOMPARE(dropped(item("oak_door")), 1);

        // Removing a half without drops (a command) takes the other half without drops too.
        hold("oak_door", 1);
        use(door.offset(0, -1, 0), QcBlockFace::Up);
        QCOMPARE(m_content->property(at(door.offset(0, 1, 0)), u"half"_s), u"top"_s);
        chat(u"/setblock %1 %2 %3 air"_s.arg(door.x).arg(door.y + 1).arg(door.z));
        ticks(2);
        QCOMPARE(at(door), qcAirBlockId);
        QCOMPARE(dropped(item("oak_door")), 1);
    }

    void loneDoorHalvesBreakWithoutDrops()
    {
        join();
        const QcBlockPos door = m_feet.offset(1, 0, -2);
        // A lower half set by a command has no partner: it breaks with the next update and drops
        // nothing, so commands cannot duplicate doors.
        chat(u"/setblock %1 %2 %3 oak_door"_s.arg(door.x).arg(door.y).arg(door.z));
        QVERIFY(m_client->chatLines.last().startsWith(u"Set qtcraft:oak_door"_s));
        ticks(2);
        QCOMPARE(at(door), qcAirBlockId);
        QCOMPARE(dropped(item("oak_door")), 0);

        // A whole door replaced half by half with stone leaves no door item either.
        QVERIFY(m_server->world().setBlock(door, id(u"oak_door"_s)));
        QVERIFY(m_server->world().setBlock(door.offset(0, 1, 0), id(u"oak_door[half=top]"_s)));
        ticks(2);
        QCOMPARE(m_content->property(at(door.offset(0, 1, 0)), u"half"_s), u"top"_s);
        chat(u"/setblock %1 %2 %3 stone"_s.arg(door.x).arg(door.y + 1).arg(door.z));
        ticks(2);
        QCOMPARE(at(door), qcAirBlockId);
        QCOMPARE(dropped(item("oak_door")), 0);
    }

    void connectionsFollowNeighbours()
    {
        join();
        const QcBlockPos a = m_feet.offset(-1, 0, -2);
        chat(u"/fill %1 %2 %3 %4 %2 %3 oak_fence"_s.arg(a.x).arg(a.y).arg(a.z).arg(a.x + 2));
        ticks(1);
        QCOMPARE(nameAt(a), u"qtcraft:oak_fence[east=true,north=false,south=false,west=false]"_s);
        QCOMPARE(nameAt(a.offset(1, 0, 0)), u"qtcraft:oak_fence[east=true,north=false,south=false,west=true]"_s);

        // A full solid block connects, a torch does not.
        m_server->world().setBlock(a.offset(1, 0, -1), id(u"stone"_s));
        chat(u"/setblock %1 %2 %3 stone"_s.arg(a.x + 1).arg(a.y).arg(a.z - 1));
        chat(u"/setblock %1 %2 %3 torch"_s.arg(a.x + 1).arg(a.y).arg(a.z + 1));
        ticks(1);
        QCOMPARE(m_content->property(at(a.offset(1, 0, 0)), u"north"_s), u"true"_s);
        QCOMPARE(m_content->property(at(a.offset(1, 0, 0)), u"south"_s), u"false"_s);

        // Removing the middle fence disconnects its neighbours.
        chat(u"/setblock %1 %2 %3 air"_s.arg(a.x + 1).arg(a.y).arg(a.z));
        ticks(1);
        QCOMPARE(m_content->property(at(a), u"east"_s), u"false"_s);
        QCOMPARE(m_content->property(at(a.offset(2, 0, 0)), u"west"_s), u"false"_s);

        // Walls show their post except in the middle of a straight run.
        const QcBlockPos w = m_feet.offset(-2, 0, 2);
        chat(u"/fill %1 %2 %3 %4 %2 %3 cobblestone_wall"_s.arg(w.x).arg(w.y).arg(w.z).arg(w.x + 2));
        ticks(1);
        QCOMPARE(m_content->property(at(w), u"up"_s), u"true"_s);
        QCOMPARE(m_content->property(at(w.offset(1, 0, 0)), u"up"_s), u"false"_s);

        // Glass panes join each other and glass.
        const QcBlockPos p = m_feet.offset(2, 0, 2);
        chat(u"/setblock %1 %2 %3 glass_pane"_s.arg(p.x).arg(p.y).arg(p.z));
        chat(u"/setblock %1 %2 %3 glass"_s.arg(p.x).arg(p.y).arg(p.z + 1));
        ticks(1);
        QCOMPARE(m_content->property(at(p), u"south"_s), u"true"_s);

        // Settled: further ticks change nothing.
        ticks(1);
        const quint64 before = m_server->processedBlockUpdates();
        ticks(3);
        QCOMPARE(m_server->pendingBlockUpdates(), 0);
        QCOMPARE(m_server->processedBlockUpdates(), before);
    }

    void unsupportedBlocksBreakAndDrop()
    {
        join();
        face(0.0f);
        hold("torch", 2);
        const QcBlockPos wall = m_feet.offset(0, 0, -3);
        m_server->world().setBlock(wall, id(u"stone"_s));
        use(wall, QcBlockFace::South);
        QCOMPARE(nameAt(wall.offset(0, 0, 1)), u"qtcraft:torch[attach=north]"_s);

        hold("ladder", 1);
        m_server->world().setBlock(wall.offset(0, 1, 0), id(u"stone"_s));
        use(wall.offset(0, 1, 0), QcBlockFace::South);
        QCOMPARE(nameAt(wall.offset(0, 1, 1)), u"qtcraft:ladder[attach=north]"_s);

        chat(u"/setblock %1 %2 %3 air"_s.arg(wall.x).arg(wall.y).arg(wall.z));
        chat(u"/setblock %1 %2 %3 air"_s.arg(wall.x).arg(wall.y + 1).arg(wall.z));
        ticks(1);
        QCOMPARE(at(wall.offset(0, 0, 1)), qcAirBlockId);
        QCOMPARE(at(wall.offset(0, 1, 1)), qcAirBlockId);
        QCOMPARE(dropped(item("torch")), 1);
        QCOMPARE(dropped(item("ladder")), 1);
    }

    void cascadesAreBoundedPerTick()
    {
        join();
        // Three columns of 180 blocks that each need the one below, far enough apart not to share
        // neighbours: about six updates per block, more than three ticks may do.
        constexpr int height = 180;
        const QList<QcBlockPos> bases{m_feet.offset(3, 0, 3), m_feet.offset(-3, 0, 3), m_feet.offset(3, 0, -3)};
        for (const QcBlockPos &base : bases) {
            for (int y = 0; y < height; ++y) {
                QVERIFY(m_server->world().setBlock(base.offset(0, y, 0), id(u"test:stack"_s)));
            }
        }
        const quint64 before = m_server->processedBlockUpdates();
        for (const QcBlockPos &base : bases) {
            m_client->connection.send(QcMsg::ChatInput{u"/setblock %1 %2 %3 air"_s.arg(base.x).arg(base.y).arg(base.z)});
        }
        QcTestUtil::pump();

        // The commands ran before the next tick; that tick and the following ones share the work.
        int ticksNeeded = 0;
        while (m_server->pendingBlockUpdates() > 0 && ticksNeeded < 20) {
            const quint64 start = m_server->processedBlockUpdates();
            ticks(1);
            QVERIFY(m_server->processedBlockUpdates() - start <= quint64(QcServer::maxBlockUpdatesPerTick));
            ++ticksNeeded;
        }
        QCOMPARE(m_server->pendingBlockUpdates(), 0);

        // Each broken block queues its six neighbours once; the work is at most 6 per block.
        const quint64 work = m_server->processedBlockUpdates() - before;
        QVERIFY2(work <= quint64(bases.size()) * quint64(6 * height + 6), qPrintable(QString::number(work)));
        QVERIFY2(work > quint64(2 * QcServer::maxBlockUpdatesPerTick), qPrintable(QString::number(work)));

        // More than two ticks' worth of updates: the cascade spread over at least three ticks.
        QVERIFY2(ticksNeeded >= 3, qPrintable(QString::number(ticksNeeded)));
        for (const QcBlockPos &base : bases) {
            for (int y = 0; y < height; ++y) {
                QCOMPARE(at(base.offset(0, y, 0)), qcAirBlockId);
            }
        }
    }

    void connectionsWaitForUnloadedNeighbours()
    {
        join();
        // Two chunks east of everything the player keeps loaded; only the western one is loaded.
        const QcChunkPos west{m_feet.chunk().x + 4, m_feet.chunk().z};
        const QcChunkPos east{west.x + 1, west.z};
        QVERIFY(m_server->world().loadChunk(west));
        QVERIFY(!m_server->world().chunk(east));
        const int x = west.x * QcWorld::chunkSize + QcWorld::chunkSize - 1;
        const int z = west.z * QcWorld::chunkSize + 5;

        // Above the terrain, so the eastern neighbour is air once it loads.
        const int y = QcTestTerrainGenerator::surfaceHeight(x + 1, z) + 2;
        const QcBlockPos fence{x, y, z};

        // A fence on the border that was connected east when it was built.
        QVERIFY(m_server->world().setBlock(fence, id(u"oak_fence[east=true]"_s)));
        // A change next to it updates the fence; the unloaded side keeps its connection.
        chat(u"/setblock %1 %2 %3 stone"_s.arg(x - 1).arg(y).arg(z));
        ticks(1);
        QCOMPARE(m_content->property(at(fence), u"west"_s), u"true"_s);
        QCOMPARE(m_content->property(at(fence), u"east"_s), u"true"_s);

        // An update for a position in the unloaded chunk waits for it instead of being lost.
        chat(u"/setblock %1 %2 %3 stone"_s.arg(x).arg(y + 1).arg(z));
        chat(u"/setblock %1 %2 %3 air"_s.arg(x).arg(y + 1).arg(z));
        ticks(1);
        QCOMPARE(m_server->pendingBlockUpdates(), 0);
        QCOMPARE(m_server->waitingBlockUpdates(), 2);

        // Loading the neighbour re-derives the border: there is only air to the east now.
        QVERIFY(m_server->world().loadChunk(east));
        ticks(1);
        QCOMPARE(m_content->property(at(fence), u"east"_s), u"false"_s);
        QCOMPARE(m_server->pendingBlockUpdates(), 0);
        QCOMPARE(m_server->waitingBlockUpdates(), 0);

        // Hanging on a block across the border counts as supported while that chunk is away.
        m_server->world().unloadChunk(east);
        QVERIFY(!m_server->world().chunk(east));
        const QcBlockPos torch = fence.offset(0, 2, 0);
        QVERIFY(m_server->world().setBlock(torch, id(u"torch[attach=east]"_s)));
        chat(u"/setblock %1 %2 %3 stone"_s.arg(x - 1).arg(torch.y).arg(z));
        ticks(1);
        QCOMPARE(nameAt(torch), u"qtcraft:torch[attach=east]"_s);
    }

    void playerUpdatesGoBeforeCommandBacklogs()
    {
        join();

        // Two fences one block apart; a big /fill above everything queues far more than one tick.
        const QcBlockPos fence = m_feet.offset(2, 0, 0);
        QVERIFY(m_server->world().setBlock(fence, id(u"oak_fence"_s)));
        QVERIFY(m_server->world().setBlock(fence.offset(2, 0, 0), id(u"oak_fence"_s)));
        const QcChunkPos low{m_feet.chunk().x - 1, m_feet.chunk().z - 1};
        const int x0 = low.x * QcWorld::chunkSize;
        const int z0 = low.z * QcWorld::chunkSize;
        chat(u"/fill %1 %2 %3 %4 %5 %6 torch"_s.arg(x0).arg(m_feet.y + 2).arg(z0).arg(x0 + 47).arg(m_feet.y + 4).arg(z0 + 47));
        // Large fills wait for /confirm (decision E-245); sent without a tick so nothing is worked off yet.
        m_client->connection.send(QcMsg::ChatInput{u"/confirm"_s});
        QcTestUtil::pump();
        QVERIFY2(m_server->pendingCommandBlockUpdates() > qsizetype(10) * QcServer::maxBlockUpdatesPerTick,
                 qPrintable(QString::number(m_server->pendingCommandBlockUpdates())));

        // A fence placed by the player between them connects in the very next tick.
        face(90.0f);
        hold("oak_fence");
        use(fence.offset(1, -1, 0), QcBlockFace::Up);
        QCOMPARE(nameAt(fence.offset(1, 0, 0)), u"qtcraft:oak_fence[east=true,north=false,south=false,west=true]"_s);
        ticks(1);
        QCOMPARE(m_content->property(at(fence), u"east"_s), u"true"_s);
        QCOMPARE(m_content->property(at(fence.offset(2, 0, 0)), u"west"_s), u"true"_s);
        QVERIFY(m_server->pendingCommandBlockUpdates() > 0);
    }

    void pendingUpdatesSurviveARestart()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        join(config);

        // Floating torches over the loaded 3 x 3 chunks: far more updates than one tick handles.
        const QcChunkPos corner{m_feet.chunk().x - 1, m_feet.chunk().z - 1};
        const QcBlockPos low{corner.x * QcWorld::chunkSize, m_feet.y + 2, corner.z * QcWorld::chunkSize};
        const QcBlockPos high = low.offset(47, 2, 47);
        chat(u"/fill %1 %2 %3 %4 %5 %6 torch"_s.arg(low.x).arg(low.y).arg(low.z).arg(high.x).arg(high.y).arg(high.z));
        // Large fills wait for /confirm (decision E-245); sent without a tick so nothing is worked off yet.
        m_client->connection.send(QcMsg::ChatInput{u"/confirm"_s});
        QcTestUtil::pump();
        QVERIFY(m_server->pendingBlockUpdates() > 0);
        QVERIFY(m_server->save().has_value());
        const auto stored = QcWorldStorage(directory.path(), m_content).readBlockUpdates();
        QVERIFY(stored.has_value());
        QCOMPARE(stored->size(), m_server->pendingBlockUpdates() + m_server->waitingBlockUpdates() + m_server->liquids().plannedCount());

        // Over the cleared stage nothing holds the torches (elsewhere hills may reach up to them).
        const auto floating = [&] {
            int count = 0;
            for (int y = low.y; y <= high.y; ++y) {
                for (int dz = -4; dz <= 4; ++dz) {
                    for (int dx = -4; dx <= 4; ++dx) {
                        count += at({m_feet.x + dx, y, m_feet.z + dz}) != qcAirBlockId ? 1 : 0;
                    }
                }
            }
            return count;
        };
        QVERIFY(floating() > 0);

        // The server goes away without another tick; the saved queue finishes the job after loading.
        cleanup();
        join(config, false);
        for (int tick = 0; tick < 200 && m_server->pendingBlockUpdates() > 0; ++tick) {
            ticks(1);
        }
        QCOMPARE(m_server->pendingBlockUpdates(), 0);
        QCOMPARE(floating(), 0);

        // Only updates for chunks that are not loaded remain in the saved queue.
        QVERIFY(m_server->save().has_value());
        QCOMPARE(QcWorldStorage(directory.path(), m_content).readBlockUpdates()->size(), m_server->waitingBlockUpdates() + m_server->liquids().plannedCount());
    }

    void commandUpdatesAreCappedAndSavesTruncate()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.limits.commandBlockUpdates = 1024;
        config.limits.savedBlockUpdates = 1024;
        join(config);
        hold("stone", 5);

        // A /fill of 6912 torches would queue about seven updates per block. Sent without a tick, so
        // the queue is seen before any of it is worked off.
        const QcBlockPos low = m_feet.offset(-23, 2, -23);
        m_client->connection.send(
            QcMsg::ChatInput{u"/fill %1 %2 %3 %4 %5 %6 torch"_s.arg(low.x).arg(low.y).arg(low.z).arg(low.x + 47).arg(low.y + 2).arg(low.z + 47)});
        m_client->connection.send(QcMsg::ChatInput{u"/confirm"_s});
        QcTestUtil::pump();
        QCOMPARE(m_server->pendingCommandBlockUpdates(), qsizetype(1024));
        QVERIFY(m_server->droppedBlockUpdates() > 5000);

        // Saving keeps the first 1024 updates, writes everything else and the player first.
        QVERIFY(m_server->save().has_value());
        const QcWorldStorage storage(directory.path(), m_content);
        QCOMPARE(storage.readBlockUpdates()->size(), qsizetype(1024));
        const auto read = storage.readPlayer(u"Builder"_s);
        QVERIFY(read.has_value() && read->player.has_value());
        QCOMPARE(read->player->inventory.countOf(item("stone")), 5);
    }

    void doorsAndBlocksNeverCloseOnBodies()
    {
        join();
        face(0.0f);
        hold("oak_door");
        const QcBlockPos door = m_feet.offset(0, 0, -2);
        use(door.offset(0, -1, 0), QcBlockFace::Up, {0.2, 1.0, 0.5});
        use(door, QcBlockFace::South);
        QCOMPARE(m_content->property(at(door), u"open"_s), u"true"_s);

        // A pig in the doorway: the door cannot swing shut onto it.
        const int pig = *m_content->mobIndex(u"qtcraft:pig"_s);
        m_server->mobs().spawn(pig, {door.x + 0.5, double(door.y), door.z + 0.5}, 9000);
        use(door, QcBlockFace::South);
        QCOMPARE(m_content->property(at(door), u"open"_s), u"true"_s);
        m_server->mobs().removeAll();
        use(door, QcBlockFace::South);
        QCOMPARE(m_content->property(at(door), u"open"_s), u"false"_s);

        // Nor can a block be placed into a mob.
        const QcBlockPos cell = m_feet.offset(-2, 0, -2);
        m_server->mobs().spawn(pig, {cell.x + 0.5, double(cell.y), cell.z + 0.5}, 9001);
        hold("cobblestone");
        use(cell.offset(0, -1, 0), QcBlockFace::Up);
        QCOMPARE(at(cell), qcAirBlockId);
    }

    void chestsFollowTheirBlock()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        join(config);
        const QcBlockPos chest = m_feet.offset(0, 0, -2);
        face(0.0f);
        hold("chest");
        use(chest.offset(0, -1, 0), QcBlockFace::Up);
        QVERIFY(m_server->blockEntities().container(chest));
        for (int slot = 0; slot < 27; ++slot) {
            m_server->blockEntities().container(chest)->stacks[slot] = {item("cobblestone"), 1, 0};
        }

        // A smaller storage block in its place keeps what fits and spills the rest.
        chat(u"/setblock %1 %2 %3 test:crate"_s.arg(chest.x).arg(chest.y).arg(chest.z));
        QCOMPARE(m_server->blockEntities().container(chest)->stacks.size(), 9);
        QCOMPARE(dropped(item("cobblestone")), 18);

        // A chest whose block is set in the world directly (no player, no command) is gone as well:
        // its contents lie on the ground, also after saving and loading (decision E-357).
        QVERIFY(m_server->world().setBlock(chest, qcAirBlockId));
        QVERIFY(m_server->save().has_value());
        cleanup();
        join(config, false);
        QVERIFY(!m_server->blockEntities().container(chest));
        QCOMPARE(m_server->blockEntities().containerCount(), 0);
        QCOMPARE(dropped(item("cobblestone")), 18 + 9);
    }

    void chestsStoreDropAndPersist()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        join(config);
        face(0.0f);
        hold("chest");
        const QcBlockPos chest = m_feet.offset(0, 0, -2);
        use(chest.offset(0, -1, 0), QcBlockFace::Up);
        QCOMPARE(nameAt(chest), u"qtcraft:chest[facing=south]"_s);
        QVERIFY(m_server->blockEntities().container(chest));

        // Using it opens a screen with its 27 slots.
        use(chest, QcBlockFace::South);
        QCOMPARE(m_client->openMachine, std::optional<QcBlockPos>(chest));
        QVERIFY(m_client->machineCategory.isEmpty());
        QCOMPARE(m_client->machineStacks.size(), 27);
        chat(u"/give cobblestone 40"_s);
        const int slot = int(m_client->stacks.indexOf(QcItemStack{item("cobblestone"), 40, 0}));
        QVERIFY(slot >= 0);
        send(QcMsg::MoveInventoryItem{slot, QcMsg::MoveInventoryItem::machineInputSlot + 26, 0});
        QCOMPARE(m_client->machineStacks[26], (QcItemStack{item("cobblestone"), 40, 0}));
        QCOMPARE(m_server->blockEntities().container(chest)->stacks[26], (QcItemStack{item("cobblestone"), 40, 0}));

        // Slots past the chest's 27 do not exist.
        send(QcMsg::MoveInventoryItem{QcMsg::MoveInventoryItem::machineInputSlot + 26, QcMsg::MoveInventoryItem::machineInputSlot + 27, 0});
        QCOMPARE(m_server->blockEntities().container(chest)->stacks[26].count, 40);

        // Saved with the world and back after a restart.
        QVERIFY(m_server->save().has_value());
        cleanup();
        join(config, false);
        QVERIFY(m_server->blockEntities().container(chest));
        QCOMPARE(m_server->blockEntities().container(chest)->stacks[26], (QcItemStack{item("cobblestone"), 40, 0}));
        QCOMPARE(nameAt(chest), u"qtcraft:chest[facing=south]"_s);

        // Breaking it spills the contents.
        chat(u"/setblock %1 %2 %3 air"_s.arg(chest.x).arg(chest.y).arg(chest.z));
        QVERIFY(!m_server->blockEntities().container(chest));
        QCOMPARE(dropped(item("cobblestone")), 40);
    }
};

QTEST_GUILESS_MAIN(TestBuilding)
#include "test_building.moc"
