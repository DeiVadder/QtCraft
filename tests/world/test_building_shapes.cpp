#include "common/blockrules/QcBlockRules.h"
#include "common/gameplay/QcCrafting.h"
#include "common/gameplay/QcMining.h"
#include "QcCircuitFixture.h"

#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

// Stair corners and vertical slabs (decisions E-754 and E-755): stairs take their
// corner from the stairs in front of and behind them, in every rotation, when placed and when a
// neighbour changes; their boxes follow the corner. Vertical slabs lean against the clicked face,
// two facing each other fill the cell and drop both, and they craft from and back into slabs.
class TestBuildingShapes : public QcCircuitFixture
{
    Q_OBJECT

private:
    QcItemId baseItem(const QString &name) const { return item(qPrintable(u"qtcraft:"_s + name)); }

    void hold(const QString &name, int count = 1)
    {
        chat(u"/give %1 %2"_s.arg(name).arg(count));
        const int slot = slotOf(baseItem(name));
        QVERIFY(slot >= 0 && slot < QcInventory::hotbarSize);
        send(QcMsg::SelectHotbarSlot{slot});
    }

    void face(float yaw)
    {
        QcPlayerInput input;
        input.sequence = ++m_sequence;
        input.yaw = yaw;
        send(QcMsg::PlayerInput{input});
    }

    void mine(QcBlockPos pos)
    {
        const QcBlockDefinition &block = m_content->block(*m_server->world().blockAt(pos));
        const QcItemStack &held = m_client->stacks[m_client->selected];
        const QcItemDefinition *tool = held.isEmpty() ? nullptr : &m_content->item(held.item);
        send(QcMsg::StartMining{pos});
        ticks(int(std::ceil(QcMining::breakSeconds(block, tool) * QcPhysics::tickRate)) + 1);
        send(QcMsg::FinishMining{pos});
        QCOMPARE(*m_server->world().blockAt(pos), qcAirBlockId);
    }

    std::pair<QString, int> crafted(const QList<std::pair<int, QString>> &cells, int size) const
    {
        QList<QcItemStack> grid(qsizetype(size) * size);
        for (const auto &[cell, name] : cells) {
            grid[cell] = {baseItem(name), 1, 0};
        }
        const std::optional<qsizetype> recipe = QcCrafting::match(*m_content, grid, size);
        if (!recipe) {
            return {};
        }
        return {m_content->recipes()[*recipe].resultItem, m_content->recipes()[*recipe].resultCount};
    }

    // The shape the stair at local (x, y, z) takes from its neighbours now.
    QString stairShape(int x, int y, int z) const
    {
        const QcBlockPos pos = at(x, y, z);
        const QcBlockId shaped = QcBlockRules::connectedState(m_server->world(), pos, *m_server->world().blockAt(pos));
        return m_content->property(shaped, u"shape"_s);
    }

    QString propertyAt(QcBlockPos pos, const QString &name) const { return m_content->property(*m_server->world().blockAt(pos), name); }

    // Whether the state has a box with these corners.
    bool hasBox(QcBlockId state, const QcAabb &box) const { return m_content->state(state).boxes.contains(box); }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void stairsTakeCornersFromTheirNeighbours_data()
    {
        QTest::addColumn<int>("turns");
        for (int turns = 0; turns < 4; ++turns) {
            QTest::addRow("turned %d", turns) << turns;
        }
    }

    void stairsTakeCornersFromTheirNeighbours()
    {
        QFETCH(int, turns);
        buildArea();
        m_turns = turns;
        // The stair under test climbs north at (5, 0, 5); in front of it is (5, 0, 4), behind (5, 0, 6).
        put(5, 0, 5, u"oak_stairs[facing=north,half=bottom]"_s);
        QCOMPARE(stairShape(5, 0, 5), u"straight"_s);

        put(5, 0, 4, u"stone_stairs[facing=west,half=bottom]"_s);
        QCOMPARE(stairShape(5, 0, 5), u"outer_left"_s);
        put(5, 0, 4, u"stone_stairs[facing=east,half=bottom]"_s);
        QCOMPARE(stairShape(5, 0, 5), u"outer_right"_s);
        // Another half or the same direction makes no corner.
        put(5, 0, 4, u"stone_stairs[facing=east,half=top]"_s);
        QCOMPARE(stairShape(5, 0, 5), u"straight"_s);
        put(5, 0, 4, u"stone_stairs[facing=north,half=bottom]"_s);
        QCOMPARE(stairShape(5, 0, 5), u"straight"_s);

        // The outer corner checks the side the front stair turns away from (its back, as in the
        // reference): the same stair there continues a straight run, one on the other side does not.
        put(5, 0, 4, u"stone_stairs[facing=west,half=bottom]"_s);
        put(6, 0, 5, u"oak_stairs[facing=north,half=bottom]"_s);
        QCOMPARE(stairShape(5, 0, 5), u"straight"_s);
        put(6, 0, 5, u"air"_s);
        put(4, 0, 5, u"oak_stairs[facing=north,half=bottom]"_s);
        QCOMPARE(stairShape(5, 0, 5), u"outer_left"_s);
        put(4, 0, 5, u"air"_s);
        put(5, 0, 4, u"air"_s);

        put(5, 0, 6, u"oak_stairs[facing=west,half=bottom]"_s);
        QCOMPARE(stairShape(5, 0, 5), u"inner_left"_s);
        put(5, 0, 6, u"oak_stairs[facing=east,half=bottom]"_s);
        QCOMPARE(stairShape(5, 0, 5), u"inner_right"_s);

        // In a straight run (the same stair beside it on the corner's side) it stays straight.
        put(6, 0, 5, u"oak_stairs[facing=north,half=bottom]"_s);
        QCOMPARE(stairShape(5, 0, 5), u"straight"_s);
        put(6, 0, 5, u"oak_stairs[facing=north,half=top]"_s);
        QCOMPARE(stairShape(5, 0, 5), u"inner_right"_s);

        // Upside down the same rules hold.
        put(5, 0, 5, u"oak_stairs[facing=north,half=top]"_s);
        put(5, 0, 6, u"oak_stairs[facing=west,half=top]"_s);
        QCOMPARE(stairShape(5, 0, 5), u"inner_left"_s);
    }

    void cornerBoxesFollowTheShape()
    {
        const QcBlockId stairs = id("qtcraft:oak_stairs");
        const auto shaped = [&](const QString &facing, const QString &half, const QString &shape) {
            return m_content->withProperty(m_content->withProperty(m_content->withProperty(stairs, u"facing"_s, facing), u"half"_s, half),
                                           u"shape"_s, shape);
        };
        const QcAabb slab{{0, 0, 0}, {1, 0.5, 1}};

        // Facing north: the step on the north half; an outer corner keeps one quarter of it, an inner
        // corner adds one quarter to it, left being west.
        const QcBlockId straight = shaped(u"north"_s, u"bottom"_s, u"straight"_s);
        QVERIFY(hasBox(straight, slab));
        QVERIFY(hasBox(straight, {{0, 0.5, 0}, {1, 1, 0.5}}));
        const QcBlockId outerLeft = shaped(u"north"_s, u"bottom"_s, u"outer_left"_s);
        QCOMPARE(m_content->state(outerLeft).boxes.size(), qsizetype(2));
        QVERIFY(hasBox(outerLeft, {{0, 0.5, 0}, {0.5, 1, 0.5}}));
        QVERIFY(hasBox(shaped(u"north"_s, u"bottom"_s, u"outer_right"_s), {{0.5, 0.5, 0}, {1, 1, 0.5}}));
        QVERIFY(hasBox(shaped(u"north"_s, u"bottom"_s, u"inner_left"_s), {{0, 0.5, 0.5}, {0.5, 1, 1}}));
        QVERIFY(hasBox(shaped(u"north"_s, u"bottom"_s, u"inner_right"_s), {{0.5, 0.5, 0.5}, {1, 1, 1}}));

        // Turned east, the outer left quarter lies north-east; upside down the step hangs below.
        QVERIFY(hasBox(shaped(u"east"_s, u"bottom"_s, u"outer_left"_s), {{0.5, 0.5, 0}, {1, 1, 0.5}}));
        QVERIFY(hasBox(shaped(u"north"_s, u"top"_s, u"outer_left"_s), {{0, 0, 0}, {0.5, 0.5, 0.5}}));
        // They collide with what they show.
        QCOMPARE(m_content->state(outerLeft).collision, m_content->state(outerLeft).boxes);
    }

    void placedStairsTurnTheirNeighbours()
    {
        join();
        // A stair climbing north at (2, 0, 0); a stair placed behind it climbing east makes it an
        // inner corner on its right.
        const QcBlockPos corner = m_feet.offset(2, 0, 0);
        m_server->world().setBlock(corner, id("qtcraft:oak_stairs"));
        const QcBlockPos ground = m_feet.offset(2, -1, 1);
        m_server->world().setBlock(ground, id("qtcraft:stone"));

        face(90.0f);
        hold(u"oak_stairs"_s, 2);
        send(QcMsg::UseItem{ground, QcBlockFace::Up});
        const QcBlockPos placed = ground.offset(0, 1, 0);
        QCOMPARE(m_content->stateName(*m_server->world().blockAt(placed)), u"qtcraft:oak_stairs[facing=east,half=bottom,shape=straight]"_s);
        ticks(2);
        QCOMPARE(propertyAt(corner, u"shape"_s), u"inner_right"_s);

        // Taking the new stair away makes the corner straight again.
        hold(u"iron_axe"_s);
        mine(placed);
        ticks(2);
        QCOMPARE(propertyAt(corner, u"shape"_s), u"straight"_s);
    }

    void aUseItemViewOf360IsAcceptedAndWrapped()
    {
        join();
        const QcBlockPos ground = m_feet.offset(2, -1, 1);
        m_server->world().setBlock(ground, id("qtcraft:stone"));

        // The player's own view faces east; the view sent with UseItem (yaw 360 = north, pitch beyond
        // the range) is wrapped and clamped instead of closing the connection (decision E-853).
        face(90.0f);
        hold(u"oak_stairs"_s, 2);
        send(QcMsg::UseItem{ground, QcBlockFace::Up, {0.5, 1.0, 0.5}, 0, QcMsg::UseItem::View{360.0f, -120.0f}});
        QCOMPARE(propertyAt(ground.offset(0, 1, 0), u"facing"_s), u"north"_s);
    }

    void verticalSlabsCraftFromSlabs()
    {
        QCOMPARE(crafted({{0, u"oak_slab"_s}, {3, u"oak_slab"_s}, {6, u"oak_slab"_s}}, 3), (std::pair{u"qtcraft:oak_vertical_slab"_s, 3}));
        QCOMPARE(crafted({{0, u"oak_vertical_slab"_s}}, 2), (std::pair{u"qtcraft:oak_slab"_s, 1}));
        QCOMPARE(crafted({{1, u"stone_bricks_slab"_s}, {4, u"stone_bricks_slab"_s}, {7, u"stone_bricks_slab"_s}}, 3),
                 (std::pair{u"qtcraft:stone_bricks_vertical_slab"_s, 3}));

        // Every material with slabs has them, carpets' materials not.
        for (const QString &material : {u"oak"_s, u"birch"_s, u"spruce"_s, u"cobblestone"_s, u"stone"_s, u"brick"_s, u"sandstone"_s,
                                        u"polished_stone"_s, u"smooth_sandstone"_s, u"mud_bricks"_s, u"mossy_stone_bricks"_s,
                                        u"mossy_cobblestone"_s, u"smooth_stone"_s, u"cut_sandstone"_s}) {
            QVERIFY2(m_content->blockId(u"qtcraft:"_s + material + u"_vertical_slab"_s), qPrintable(material));
            QVERIFY2(m_content->blockId(u"qtcraft:"_s + material + u"_slab"_s), qPrintable(material));
        }
        QVERIFY(!m_content->blockId(u"qtcraft:white_wool_vertical_slab"_s));
        QCOMPARE(m_content->block(id("qtcraft:oak_vertical_slab")).fuelSeconds, m_content->block(id("qtcraft:oak_slab")).fuelSeconds);
    }

    void verticalSlabsLeanAgainstTheClickedFace()
    {
        join();
        const QcBlockPos base = m_feet.offset(2, 0, 0);
        m_server->world().setBlock(base, id("qtcraft:stone"));
        hold(u"oak_vertical_slab"_s, 8);

        // Against the west face (towards the player): it stands on the east side of its cell, against
        // the stone.
        send(QcMsg::UseItem{base, QcBlockFace::West});
        const QcBlockPos slab = base.offset(-1, 0, 0);
        QCOMPARE(propertyAt(slab, u"facing"_s), u"east"_s);
        QCOMPARE(propertyAt(slab, u"double"_s), u"false"_s);
        const QcBlockState &single = m_content->state(*m_server->world().blockAt(slab));
        QCOMPARE(single.boxes, (QList<QcAabb>{{{0.5, 0, 0}, {1, 1, 1}}}));
        QVERIFY(single.fullFace[std::size_t(QcBlockFace::East)]);
        QVERIFY(!single.fullFace[std::size_t(QcBlockFace::Up)]);

        // Clicked on its open side, it fills the cell.
        send(QcMsg::UseItem{slab, QcBlockFace::West});
        QCOMPARE(propertyAt(slab, u"double"_s), u"true"_s);
        QVERIFY(m_content->state(*m_server->world().blockAt(slab)).fullCube);
        QCOMPARE(*m_server->world().blockAt(slab.offset(-1, 0, 0)), qcAirBlockId);

        // A full one drops both halves.
        hold(u"iron_axe"_s);
        mine(slab);
        ticks(2);
        QCOMPARE(droppedCount(baseItem(u"oak_vertical_slab"_s)), 2);
    }

    void verticalSlabsStandWhereThePlayerLooks()
    {
        join();
        const QcBlockId slab = id("qtcraft:stone_vertical_slab");
        const QcBlockPos base = m_feet.offset(2, 0, 0);
        m_server->world().setBlock(base, id("qtcraft:stone"));
        const QcBlockPos top = base.offset(0, 1, 0);

        // On the top or bottom of a block: on the far side of the cell as the player looks.
        for (const auto &[yaw, facing] : {std::pair{0.0F, u"north"_s}, std::pair{90.0F, u"east"_s}, std::pair{180.0F, u"south"_s},
                                          std::pair{270.0F, u"west"_s}}) {
            const std::optional<QcBlockRules::Placement> placed = QcBlockRules::placement(m_server->world(), slab, {base, QcBlockFace::Up, {0.5, 1, 0.5}, yaw, -30.0F});
            QVERIFY(placed);
            QCOMPARE(placed->pos, top);
            QCOMPARE(m_content->property(placed->state, u"facing"_s), facing);
        }

        // Placed into a cell whose vertical slab faces the other way, both fill it; the same way or
        // across, nothing is placed.
        m_server->world().setBlock(top, m_content->withProperty(slab, u"facing"_s, u"north"_s));
        const QcBlockPos south = top.offset(0, 0, 1);
        m_server->world().setBlock(south, id("qtcraft:stone"));
        const std::optional<QcBlockRules::Placement> filled = QcBlockRules::placement(m_server->world(), slab, {south, QcBlockFace::North, {0.5, 0.5, 0}, 0.0F, 0.0F});
        QVERIFY(filled);
        QCOMPARE(filled->pos, top);
        QCOMPARE(m_content->stateName(filled->state), u"qtcraft:stone_vertical_slab[double=true,facing=north]"_s);

        m_server->world().setBlock(top, m_content->withProperty(slab, u"facing"_s, u"south"_s));
        QVERIFY(!QcBlockRules::placement(m_server->world(), slab, {south, QcBlockFace::North, {0.5, 0.5, 0}, 0.0F, 0.0F}));
        m_server->world().setBlock(top, m_content->withProperty(slab, u"facing"_s, u"east"_s));
        QVERIFY(!QcBlockRules::placement(m_server->world(), slab, {south, QcBlockFace::North, {0.5, 0.5, 0}, 0.0F, 0.0F}));
    }
};

QTEST_GUILESS_MAIN(TestBuildingShapes)
#include "test_building_shapes.moc"
