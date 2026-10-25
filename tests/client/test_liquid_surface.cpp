#include "clientcore/meshing/QcLiquidSurface.h"

#include <QTest>

#include <array>

// The corner heights of a liquid's surface (decision E-885): flat among sources and against walls,
// sloping towards lower flows and open cells, full beside liquid with liquid on top, the same at the
// corners two cells share.
namespace {

constexpr float full = 0.875f;
constexpr float open = QcLiquidSurface::open;
constexpr float wall = QcLiquidSurface::closed;

// Corner indices (x + 2 * z).
constexpr std::size_t northWest = 0;
constexpr std::size_t northEast = 1;
constexpr std::size_t southWest = 2;
constexpr std::size_t southEast = 3;

// The 3 x 3 cells around (x, z) of a field indexed [z][x].
template <std::size_t W, std::size_t H>
std::array<float, 9> around(const std::array<std::array<float, W>, H> &field, std::size_t x, std::size_t z)
{
    std::array<float, 9> cells{};
    for (std::size_t dz = 0; dz < 3; ++dz) {
        for (std::size_t dx = 0; dx < 3; ++dx) {
            cells[dz * 3 + dx] = field[z + dz - 1][x + dx - 1];
        }
    }
    return cells;
}

} // namespace

class TestLiquidSurface : public QObject
{
    Q_OBJECT

private slots:
    void sourcesAmongSourcesAndWallsStayFlat()
    {
        const std::array<float, 4> lake = QcLiquidSurface::cornerHeights({full, full, full, full, full, full, full, full, full});
        QCOMPARE(lake, (std::array<float, 4>{full, full, full, full}));

        // A source in a basin of stone: the walls do not count.
        const std::array<float, 4> basin = QcLiquidSurface::cornerHeights({wall, wall, wall, wall, full, wall, wall, wall, wall});
        QCOMPARE(basin, (std::array<float, 4>{full, full, full, full}));
    }

    void theSurfaceFallsTowardsLowerFlowAndOpenCells()
    {
        // A flow of 0.3 between a source to its west and air to its east, walls north and south.
        const std::array<float, 4> flow = QcLiquidSurface::cornerHeights({wall, wall, wall, full, 0.3f, open, wall, wall, wall});
        QVERIFY(flow[northWest] > flow[northEast]);
        QVERIFY(flow[southWest] > flow[southEast]);
        QCOMPARE(flow[northWest], flow[southWest]);
        // The source weighs ten times: (10 * 0.875 + 0.3) / 11.
        QCOMPARE(flow[northWest], (10.0f * full + 0.3f) / 11.0f);
        // Towards the air: (0.3 + 0) / 2.
        QCOMPARE(flow[northEast], 0.15f);
    }

    void liquidOnTopOfANeighbourFillsTheCorner()
    {
        // The cell to the east has liquid on top (height 1): the eastern corners are full.
        const std::array<float, 4> corners = QcLiquidSurface::cornerHeights({wall, wall, wall, wall, 0.4f, 1.0f, wall, wall, wall});
        QCOMPARE(corners[northEast], 1.0f);
        QCOMPARE(corners[southEast], 1.0f);
        QCOMPARE(corners[northWest], 0.4f);
    }

    void theDiagonalCountsOnlyPastALiquidSide()
    {
        // Air on both sides of the north-west corner: the source diagonally beyond does not reach it.
        const std::array<float, 4> cut = QcLiquidSurface::cornerHeights({full, open, wall, open, 0.5f, wall, wall, wall, wall});
        QCOMPARE(cut[northWest], 0.5f / 3.0f);
        // With liquid on one side it does.
        const std::array<float, 4> reached = QcLiquidSurface::cornerHeights({full, 0.5f, wall, open, 0.5f, wall, wall, wall, wall});
        QCOMPARE(reached[northWest], (10.0f * full + 0.5f + 0.5f + 0.0f) / 13.0f);
    }

    void neighboursAgreeOnTheCornersTheyShare()
    {
        // A field of flows, sources, air and walls, one cell of margin all round. Neighbours along x
        // and z share two corners; diagonal neighbours share one where the liquid reaches past a side.
        const std::array<std::array<float, 6>, 5> field{{
            {wall, wall, wall, wall, wall, wall},
            {wall, full, 0.7f, 0.5f, open, wall},
            {wall, full, 0.6f, 0.3f, 0.1f, wall},
            {wall, 1.0f, 0.5f, open, 0.2f, wall},
            {wall, wall, wall, wall, wall, wall},
        }};
        const auto liquid = [&field](std::size_t x, std::size_t z) { return field[z][x] > 0.0f; };
        const auto corners = [&field](std::size_t x, std::size_t z) { return QcLiquidSurface::cornerHeights(around(field, x, z)); };
        int compared = 0;
        for (std::size_t z = 1; z + 1 < field.size(); ++z) {
            for (std::size_t x = 1; x + 1 < field[z].size(); ++x) {
                if (!liquid(x, z)) {
                    continue;
                }
                const std::array<float, 4> own = corners(x, z);
                if (liquid(x + 1, z)) {
                    QCOMPARE(own[northEast], corners(x + 1, z)[northWest]);
                    QCOMPARE(own[southEast], corners(x + 1, z)[southWest]);
                    ++compared;
                }
                if (z + 2 < field.size() && liquid(x, z + 1)) {
                    QCOMPARE(own[southWest], corners(x, z + 1)[northWest]);
                    QCOMPARE(own[southEast], corners(x, z + 1)[northEast]);
                    ++compared;
                }
                const bool sideLiquid = liquid(x + 1, z) || (z + 2 < field.size() && liquid(x, z + 1));
                if (z + 2 < field.size() && liquid(x + 1, z + 1) && sideLiquid) {
                    QCOMPARE(own[southEast], corners(x + 1, z + 1)[northWest]);
                    ++compared;
                }
                if (z + 2 < field.size() && x > 1 && liquid(x - 1, z + 1) && (liquid(x - 1, z) || liquid(x, z + 1))) {
                    QCOMPARE(own[southWest], corners(x - 1, z + 1)[northEast]);
                    ++compared;
                }
            }
        }
        // Pairs along x, along z and both diagonals were compared.
        QVERIFY(compared >= 12);
    }
};

QTEST_GUILESS_MAIN(TestLiquidSurface)
#include "test_liquid_surface.moc"
