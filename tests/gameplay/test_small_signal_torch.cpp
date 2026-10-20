#include "common/gameplay/QcCrafting.h"
#include "common/world/QcLightVolume.h"
#include "QcCircuitFixture.h"
#include "QcTestUtil.h"

using namespace Qt::StringLiterals;

// The small Signal Torch on a running server (decision E-745): an inverter like the Signal Torch,
// but with strength 7, half its light and no strong power into a block above it (a component right
// on it is still powered). Each case compares it
// with the large torch built the same way, so a test cannot pass because nothing is powered at all.
class TestSmallSignalTorch : public QcCircuitFixture
{
    Q_OBJECT

private:
    int lightAt(int x, int y, int z) const { return QcLightVolume::blockLightAt(m_server->world(), at(x, y, z)); }

    struct Crafted
    {
        QString item;
        int count = 0;
    };

    // What a crafting grid of `size` x `size` with one item in each listed cell makes (empty item
    // when no recipe matches).
    Crafted crafted(const QList<std::pair<int, const char *>> &cells, int size) const
    {
        QList<QcItemStack> grid(qsizetype(size) * size);
        for (const auto &[cell, name] : cells) {
            grid[cell] = {*m_content->itemId(u"qtcraft:"_s + QLatin1StringView(name)), 1, 0};
        }
        const std::optional<qsizetype> recipe = QcCrafting::match(*m_content, grid, size);
        return recipe ? Crafted{m_content->recipes()[*recipe].resultItem, m_content->recipes()[*recipe].resultCount} : Crafted{};
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void craftedFromSignalDustChunks()
    {
        buildArea();
        // One dust gives nine chunks, nine chunks one dust, a chunk over a stick one small torch.
        const Crafted chunks = crafted({{0, "signal_dust"}}, 2);
        QCOMPARE(chunks.item, u"qtcraft:signal_dust_chunk"_s);
        QCOMPARE(chunks.count, 9);
        const Crafted dust = crafted({{0, "signal_dust_chunk"}, {1, "signal_dust_chunk"}, {2, "signal_dust_chunk"},
                                      {3, "signal_dust_chunk"}, {4, "signal_dust_chunk"}, {5, "signal_dust_chunk"},
                                      {6, "signal_dust_chunk"}, {7, "signal_dust_chunk"}, {8, "signal_dust_chunk"}},
                                     3);
        QCOMPARE(dust.item, u"qtcraft:signal_dust"_s);
        QCOMPARE(dust.count, 1);
        const Crafted torch = crafted({{1, "signal_dust_chunk"}, {4, "stick"}}, 3);
        QCOMPARE(torch.item, u"qtcraft:small_signal_torch"_s);
        QCOMPARE(torch.count, 1);
        QCOMPARE(crafted({{0, "signal_torch"}}, 2).item, QString());
    }

    void reachesSevenDust()
    {
        buildArea();
        put(0, 0, 2, u"signal_torch[attach=floor]"_s);
        dustLine(1, 0, 2, 9);
        put(0, 0, 5, u"small_signal_torch[attach=floor]"_s);
        dustLine(1, 0, 5, 9);
        ticks(3);

        QCOMPARE(power(1, 0, 2), 15);
        QCOMPARE(power(9, 0, 2), 7);
        QCOMPARE(power(1, 0, 5), 7);
        QCOMPARE(power(7, 0, 5), 1);
        QCOMPARE(power(8, 0, 5), 0);
    }

    void powersAComponentAboveButNoBlock()
    {
        buildArea();
        // Right on the small torch: a lamp. On a stone on top of it: a lamp, dust, a lamp beside.
        put(1, 0, 2, u"small_signal_torch[attach=floor]"_s);
        put(1, 1, 2, u"signal_lamp"_s);
        put(4, 0, 2, u"small_signal_torch[attach=floor]"_s);
        put(4, 1, 2, u"stone"_s);
        put(4, 2, 2, u"signal_lamp"_s);
        put(7, 0, 2, u"small_signal_torch[attach=floor]"_s);
        put(7, 1, 2, u"stone"_s);
        put(7, 2, 2, u"signal_dust"_s);
        put(8, 1, 2, u"signal_lamp"_s);
        // The large torch with the same stones.
        put(10, 0, 2, u"signal_torch[attach=floor]"_s);
        put(10, 1, 2, u"stone"_s);
        put(10, 2, 2, u"signal_lamp"_s);
        put(10, 0, 5, u"signal_torch[attach=floor]"_s);
        put(10, 1, 5, u"stone"_s);
        put(10, 2, 5, u"signal_dust"_s);
        put(11, 1, 5, u"signal_lamp"_s);
        ticks(3);

        QVERIFY(isBlock(1, 1, 2, "signal_lamp_lit"));
        QVERIFY(isBlock(4, 2, 2, "signal_lamp"));
        QCOMPARE(power(7, 2, 2), 0);
        QVERIFY(isBlock(8, 1, 2, "signal_lamp"));
        QVERIFY(isBlock(10, 2, 2, "signal_lamp_lit"));
        QCOMPARE(power(10, 2, 5), 15);
        QVERIFY(isBlock(11, 1, 5, "signal_lamp_lit"));
    }

    void onAWallPowersAComponentAboveButNoBlock()
    {
        buildArea();
        // Wall torches hanging on a stone to their west.
        put(1, 0, 8, u"stone"_s);
        put(2, 0, 8, u"small_signal_torch[attach=west]"_s);
        put(2, 1, 8, u"signal_lamp"_s);
        put(4, 0, 8, u"stone"_s);
        put(5, 0, 8, u"small_signal_torch[attach=west]"_s);
        put(5, 1, 8, u"stone"_s);
        put(5, 2, 8, u"signal_lamp"_s);
        put(7, 0, 8, u"stone"_s);
        put(8, 0, 8, u"small_signal_torch[attach=west]"_s);
        put(8, 1, 8, u"stone"_s);
        put(8, 2, 8, u"signal_dust"_s);
        put(11, 0, 8, u"stone"_s);
        put(12, 0, 8, u"signal_torch[attach=west]"_s);
        put(12, 1, 8, u"stone"_s);
        put(12, 2, 8, u"signal_lamp"_s);
        ticks(3);

        QVERIFY(isBlock(2, 1, 8, "signal_lamp_lit"));
        QVERIFY(isBlock(5, 0, 8, "small_signal_torch"));
        QVERIFY(isBlock(5, 2, 8, "signal_lamp"));
        QCOMPARE(power(8, 2, 8), 0);
        QVERIFY(isBlock(12, 2, 8, "signal_lamp_lit"));
    }

    void pistonsAboveItExtendAndAnObserverStaysStill()
    {
        buildArea();
        put(2, 0, 2, u"small_signal_torch[attach=floor]"_s);
        put(2, 1, 2, u"signal_piston[facing=up]"_s);
        put(5, 0, 2, u"small_signal_torch[attach=floor]"_s);
        put(5, 1, 2, u"sticky_signal_piston[facing=up]"_s);
        put(2, 0, 6, u"small_signal_torch[attach=floor]"_s);
        put(2, 1, 6, u"signal_observer[facing=up]"_s);

        // The observer would pulse within 4 ticks; watch every tick.
        for (int tick = 0; tick < 6; ++tick) {
            ticks(1);
            QVERIFY(!flag(2, 1, 6, u"powered"_s));
        }
        QVERIFY(flag(2, 1, 2, u"extended"_s));
        QVERIFY(flag(5, 1, 2, u"extended"_s));
    }

    void invertsTwoTicksLaterAndDims()
    {
        buildArea();
        put(5, 0, 5, u"stone"_s);
        put(5, 0, 4, u"lever[attach=south]"_s);
        put(6, 0, 5, u"small_signal_torch[attach=west]"_s);
        put(13, 0, 12, u"signal_torch[attach=floor]"_s);
        ticks(3);
        QVERIFY(isBlock(6, 0, 5, "small_signal_torch"));
        QCOMPARE(lightAt(6, 0, 5), 4);
        QCOMPARE(lightAt(13, 0, 12), 7);

        flip(5, 0, 4);
        ticks(2);
        QVERIFY(isBlock(6, 0, 5, "small_signal_torch"));
        ticks(1);
        QVERIFY(isBlock(6, 0, 5, "small_signal_torch_off"));
        QCOMPARE(lightAt(6, 0, 5), 0);

        flip(5, 0, 4);
        ticks(3);
        QVERIFY(isBlock(6, 0, 5, "small_signal_torch"));
        QCOMPARE(lightAt(6, 0, 5), 4);
    }
};

QTEST_GUILESS_MAIN(TestSmallSignalTorch)
#include "test_small_signal_torch.moc"
