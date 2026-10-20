#include "QcTestUtil.h"
#include "world/QcVillageTestUtil.h"

#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace QcVillageTestUtil;

// The bounds of a village (decision E-403, E-409): everything of a plan stays inside its own
// region, also at the largest radius, with a small region, a wide mod lamp and a wide mod wetland;
// and every door of a building opens onto ground a player can walk out on.
class TestVillageBounds : public QObject
{
    Q_OBJECT

private:
    static constexpr qint64 seed = 42;

    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    struct Box
    {
        int x0 = 0;
        int z0 = 0;
        int x1 = 0;
        int z1 = 0;

        bool inside(Box outer) const { return x0 >= outer.x0 && z0 >= outer.z0 && x1 <= outer.x1 && z1 <= outer.z1; }
    };

    // The base content with other village settings, a lamp seven blocks wide for the plains and a
    // wetland reaching 28 blocks for the swamp.
    std::shared_ptr<const QcContentRegistry> modded(int spacing, int radius) const
    {
        auto content = std::make_shared<QcContentRegistry>(*m_content);
        QcVillageSettings settings = content->villageCatalog().settings();
        settings.spacing = spacing;
        settings.radius = radius;
        content->villageCatalog().setSettings(settings);

        QcStructureDefinition lamp;
        lamp.name = u"mod:wide_lamp"_s;
        lamp.sizeX = 7;
        lamp.sizeY = 2;
        lamp.sizeZ = 7;
        lamp.palette = {u"qtcraft:cobblestone"_s};
        lamp.cells.fill(0, qsizetype(lamp.sizeX) * lamp.sizeY * lamp.sizeZ);
        content->worldgen().registerStructure(lamp);
        const QList<QcVillageStyleDefinition> styles = content->villageCatalog().styles();
        for (QcVillageStyleDefinition style : styles) {
            if (style.name == u"qtcraft:plains"_s) {
                style.lamp = lamp.name;
            }
            if (style.name == u"qtcraft:swamp"_s) {
                style.wetland.margin = 12;
                style.wetland.fray = 8;
                style.wetland.blend = 8;
            }
            content->villageCatalog().registerStyle(style);
        }
        return content;
    }

    // Checks that everything of every plan within `rings` regions of the origin lies in its own
    // region; returns how many plans it checked.
    static int checkPlans(const QcTerrainGenerator &generator, int rings)
    {
        const int spacing = generator.villages().spacing();
        int checked = 0;
        const QList<std::shared_ptr<const QcVillagePlan>> plans = plansAround(generator, rings);
        for (const std::shared_ptr<const QcVillagePlan> &plan : plans) {
            const int regionX = QcVillagePlan::regionX(plan->id);
            const int regionZ = QcVillagePlan::regionZ(plan->id);
            const Box region{regionX * spacing, regionZ * spacing, regionX * spacing + spacing - 1, regionZ * spacing + spacing - 1};
            const Box box{plan->minX, plan->minZ, plan->maxX, plan->maxZ};
            const auto where = [&](const char *what) {
                return u"%1 of the village at %2 %3"_s.arg(QLatin1StringView(what)).arg(plan->centre.x).arg(plan->centre.z);
            };
            if (!box.inside(region)) {
                qWarning("%s", qPrintable(where("box")));
                return -1;
            }
            for (const QcStructurePlacement &piece : plan->pieces) {
                const Box rect{piece.origin().x, piece.origin().z, piece.origin().x + piece.sizeX() - 1, piece.origin().z + piece.sizeZ() - 1};
                if (!rect.inside(box)) {
                    qWarning("%s", qPrintable(where("piece")));
                    return -1;
                }
            }
            QList<QcBlockPos> cells = plan->roads + plan->branchRoads;
            for (const QcVillagePlan::Deck &deck : plan->decks) {
                cells.append(deck.at);
            }
            if (!std::ranges::all_of(cells, [&](QcBlockPos at) { return Box{at.x, at.z, at.x, at.z}.inside(region); })) {
                qWarning("%s", qPrintable(where("road")));
                return -1;
            }
            // The shaped ground ends inside the region, not in a straight line at its edge.
            const int reach = plan->wetland.settings.reach();
            for (const QcVillagePlan::Area &area : plan->wetland.wet) {
                if (plan->wetland.settings.bottom != qcAirBlockId && !Box{area.x0 - reach, area.z0 - reach, area.x1 + reach, area.z1 + reach}.inside(region)) {
                    qWarning("%s", qPrintable(where("wetland")));
                    return -1;
                }
            }
            ++checked;
        }
        return checked;
    }

    // The standing height of the column beside a door: the top of a solid block at `low` or one
    // below with room for a player above; std::nullopt when there is none.
    std::optional<int> standingAt(const Chunks &chunks, QcBlockPos at, int low) const
    {
        for (int y = low; y >= low - 1; --y) {
            const auto solid = [&](int h) {
                const std::optional<QcBlockId> block = blockIn(chunks, {at.x, h, at.z});
                return block && m_content->block(*block).solid;
            };
            if (solid(y) && !solid(y + 1) && !solid(y + 2)) {
                return y;
            }
        }
        return std::nullopt;
    }

    // The lower half of a door in the world and the direction of its passage.
    struct Door
    {
        QcBlockPos at;
        int fx = 0;
        int fz = 0;
    };

    QList<Door> doorsOf(const QcStructurePlacement &piece) const
    {
        const QcStructure &structure = piece.structure();
        QList<Door> doors;
        for (int cell = 0; cell < structure.sizeX() * structure.sizeY() * structure.sizeZ(); ++cell) {
            const QcBlockPos local{cell % structure.sizeX(), cell / (structure.sizeX() * structure.sizeZ()), cell / structure.sizeX() % structure.sizeZ()};
            const std::optional<QcBlockId> state = piece.worldBlock(local);
            if (!state || !m_content->block(*state).door || m_content->property(*state, u"half"_s) != u"bottom"_s) {
                continue;
            }
            const QString facing = m_content->property(*state, u"facing"_s);
            doors.append({piece.toWorld(local), facing == u"east"_s ? 1 : (facing == u"west"_s ? -1 : 0),
                          facing == u"south"_s ? 1 : (facing == u"north"_s ? -1 : 0)});
        }
        return doors;
    }

    // Whether a player stands on both sides of the door; a side where none does is logged.
    bool walkableBeside(const Chunks &chunks, const Door &door, const QString &piece) const
    {
        bool walkable = true;
        for (const int side : {-1, 1}) {
            const QcBlockPos beside = door.at.offset(door.fx * side, 0, door.fz * side);
            if (standingAt(chunks, beside, door.at.y - 1)) {
                continue;
            }
            QString column;
            for (int y = door.at.y - 4; y <= door.at.y + 3; ++y) {
                const std::optional<QcBlockId> block = blockIn(chunks, {beside.x, y, beside.z});
                column += u" %1:%2"_s.arg(y).arg(block ? m_content->block(*block).name : u"-"_s);
            }
            qWarning("%s door at %d %d %d, side %d:%s", qPrintable(piece), door.at.x, door.at.y, door.at.z, side, qPrintable(column));
            walkable = false;
        }
        return walkable;
    }

private slots:
    void largeVillagesStayInTheirRegion_data()
    {
        QTest::addColumn<int>("spacing");
        QTest::addColumn<int>("radius");
        QTest::addColumn<int>("rings");
        // Radii that leave the smallest margin to the region's edge, in the default regions and in small ones.
        QTest::newRow("radius 96") << 384 << 96 << 12;
        QTest::newRow("small regions") << 128 << 48 << 14;
    }

    void largeVillagesStayInTheirRegion()
    {
        QFETCH(int, spacing);
        QFETCH(int, radius);
        QFETCH(int, rings);
        const std::shared_ptr<const QcContentRegistry> content = modded(spacing, radius);
        const QcVillageStyles styles(*content);
        const QStringList &warnings = styles.warnings();
        // The wetland is fitted into the margin, with a warning.
        QVERIFY(std::ranges::any_of(warnings, [](const QString &w) { return w.contains(u"wetland reaches 28 blocks"_s); }));
        QVERIFY(styles.named(u"qtcraft:swamp"_s)->wetland.reach() <= QcVillagePlanner::freeMargin(content->villageCatalog().settings()));
        QCOMPARE(styles.named(u"qtcraft:plains"_s)->lamp.first().structure()->name(), u"mod:wide_lamp"_s);

        const QcTerrainGenerator generator(content, seed);
        const int checked = checkPlans(generator, rings);
        QVERIFY2(checked >= 4, qPrintable(u"%1 villages checked"_s.arg(checked)));
    }

    void swampVillagesAtTheLargestRadiusStayInTheirRegion()
    {
        // The swamp villages lie farther out; their wetland is the widest part of a village.
        const QcTerrainGenerator generator(modded(384, 176), seed);
        int swamps = 0;
        const QList<std::shared_ptr<const QcVillagePlan>> plans = plansAround(generator, 14);
        for (const std::shared_ptr<const QcVillagePlan> &plan : plans) {
            swamps += plan->style == u"qtcraft:swamp"_s ? 1 : 0;
        }
        QVERIFY(swamps >= 1);
        QVERIFY(checkPlans(generator, 14) >= swamps);
    }

    void doorsOpenOntoWalkableGround()
    {
        // In front of and behind every door a player stands at the floor's height or one below.
        const QcTerrainGenerator generator(m_content, seed);
        int doors = 0;
        int failures = 0;
        const QList<std::shared_ptr<const QcVillagePlan>> plans = plansAround(generator, 2);
        for (const std::shared_ptr<const QcVillagePlan> &plan : plans) {
            // Over water the doors open onto the boardwalks (test_village_swamp walks them).
            if (plan->style == u"qtcraft:swamp"_s) {
                continue;
            }
            const Chunks chunks = generateInOrder(generator, chunksOf(*plan));
            for (const QcStructurePlacement &piece : plan->pieces) {
                const QList<Door> found = doorsOf(piece);
                for (const Door &door : found) {
                    failures += walkableBeside(chunks, door, shortName(piece)) ? 0 : 1;
                }
                doors += int(found.size());
            }
        }
        QVERIFY2(doors >= 10, qPrintable(u"%1 doors"_s.arg(doors)));
        QCOMPARE(failures, 0);
    }
};

QTEST_GUILESS_MAIN(TestVillageBounds)
#include "test_village_bounds.moc"
