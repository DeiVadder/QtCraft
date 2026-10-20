#include "common/world/QcLightVolume.h"
#include "QcPlayerUseFixture.h"

using namespace Qt::StringLiterals;

// The Signal Indicator Light (decision E-765): a lamp the size of a button on any face of a block.
// It lights while the block it sits on is powered or dust points at it, glows with light 3, goes
// dark two ticks after its power ends and takes the colour of a dye used on it. Each case has an
// unpowered twin next to the powered one, so nothing passes because everything lights.
class TestSignalIndicator : public QcPlayerUseFixture
{
    Q_OBJECT

private:
    int lightAt(int x, int y, int z) const { return QcLightVolume::blockLightAt(m_server->world(), at(x, y, z)); }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void craftedFromAPaneAndAChunk()
    {
        buildArea();
        QCOMPARE(crafted({{1, u"signal_dust_chunk"_s}, {4, u"glass_pane"_s}}, 3), (std::pair{u"qtcraft:signal_indicator_light"_s, 1}));
        QCOMPARE(crafted({{1, u"signal_dust"_s}, {4, u"glass_pane"_s}}, 3), (std::pair<QString, int>{}));
    }

    void lightsOnAPoweredBlockOnEveryFace()
    {
        buildArea();
        // A stone with a lever on its top and lights on its west side and its bottom; a twin stone
        // without lever carries a light on its side.
        put(3, 1, 3, u"stone"_s);
        put(3, 2, 3, u"lever[attach=floor,powered=true]"_s);
        put(2, 1, 3, u"signal_indicator_light[attach=east]"_s);
        put(3, 0, 3, u"signal_indicator_light[attach=ceiling]"_s);
        put(8, 1, 3, u"stone"_s);
        put(7, 1, 3, u"signal_indicator_light[attach=east]"_s);
        // On the floor next to a lever on the ground: powered directly.
        put(3, 0, 7, u"lever[attach=floor,powered=true]"_s);
        put(4, 0, 7, u"signal_indicator_light[attach=floor,color=blue]"_s);
        ticks(2);

        QVERIFY(isBlock(2, 1, 3, "signal_indicator_light_lit"));
        QVERIFY(isBlock(3, 0, 3, "signal_indicator_light_lit"));
        QVERIFY(isBlock(7, 1, 3, "signal_indicator_light"));
        QCOMPARE(look(4, 0, 7), u"signal_indicator_light_lit[attach=floor,color=blue]"_s);
        QCOMPARE(lightAt(2, 1, 3), 3);
        QCOMPARE(lightAt(7, 1, 3), 0);

        // The lever off: dark two ticks later, colour and face kept.
        flip(3, 0, 7);
        ticks(1);
        QVERIFY(isBlock(4, 0, 7, "signal_indicator_light_lit"));
        ticks(3);
        QCOMPARE(look(4, 0, 7), u"signal_indicator_light[attach=floor,color=blue]"_s);
    }

    void dustPointingAtItLightsIt()
    {
        buildArea();
        put(1, 0, 4, u"signal_torch[attach=floor]"_s);
        dustLine(2, 0, 4, 3);
        put(5, 0, 4, u"signal_indicator_light[attach=floor]"_s);
        // Dust passing beside a light does not point at it.
        put(1, 0, 8, u"signal_torch[attach=floor]"_s);
        dustLine(2, 0, 8, 4);
        put(3, 0, 9, u"signal_indicator_light[attach=floor]"_s);
        ticks(3);

        QVERIFY(isBlock(5, 0, 4, "signal_indicator_light_lit"));
        QVERIFY(isBlock(3, 0, 9, "signal_indicator_light"));
    }

    void colourChoosesTheLensTexture()
    {
        buildArea();
        // The lens part names its slot by the colour ("$color"), so each colour is its own variant.
        const QcBlockState &red = m_content->state(state(u"signal_indicator_light[attach=north,color=red]"_s));
        const QcBlockState &blue = m_content->state(state(u"signal_indicator_light[attach=north,color=blue]"_s));
        QVERIFY(red.boxTextures.contains(u"red"_s));
        QVERIFY(!red.boxTextures.contains(u"blue"_s));
        QVERIFY(blue.boxTextures.contains(u"blue"_s));
        QCOMPARE(red.boxes, blue.boxes);
        const QcBlockDefinition &lit = m_content->block(state(u"signal_indicator_light_lit"_s));
        QCOMPARE(lit.slotTexture(u"red"_s), u"qtcraft:signal_indicator_lens_red_lit"_s);
        QCOMPARE(lit.lightLevel, 3);
    }

    void dyeChangesTheColour()
    {
        join();
        const QcBlockPos base = m_feet.offset(2, 0, 0);
        m_server->world().setBlock(base, id("qtcraft:stone"));
        const QcBlockPos light = base.offset(-1, 0, 0);
        m_server->world().setBlock(light, m_content->withProperty(id("qtcraft:signal_indicator_light_lit"), u"attach"_s, u"east"_s));

        hold(u"lime_dye"_s, 3);
        send(QcMsg::UseItem{light, QcBlockFace::West});
        ticks(1);
        QCOMPARE(propertyAt(light, u"color"_s), u"lime"_s);
        QCOMPARE(blockNameAt(light), u"qtcraft:signal_indicator_light_lit"_s);
        QCOMPARE(propertyAt(light, u"attach"_s), u"east"_s);
        QCOMPARE(countOf(baseItem(u"lime_dye"_s)), 2);

        // The same colour again takes no dye; another block takes none either.
        send(QcMsg::UseItem{light, QcBlockFace::West});
        send(QcMsg::UseItem{base, QcBlockFace::West});
        ticks(1);
        QCOMPARE(countOf(baseItem(u"lime_dye"_s)), 2);
        QCOMPARE(blockNameAt(base), u"qtcraft:stone"_s);

        hold(u"black_dye"_s, 1);
        send(QcMsg::UseItem{light, QcBlockFace::West});
        ticks(1);
        QCOMPARE(propertyAt(light, u"color"_s), u"black"_s);
        QCOMPARE(countOf(baseItem(u"black_dye"_s)), 0);
    }

    void dropsTheUnlitItem()
    {
        join();
        const QcBlockPos base = m_feet.offset(2, -1, 0);
        const QcBlockPos light = base.offset(0, 1, 0);
        m_server->world().setBlock(light, m_content->withProperty(id("qtcraft:signal_indicator_light_lit"), u"color"_s, u"pink"_s));
        mine(light);
        ticks(2);
        QCOMPARE(droppedCount(baseItem(u"signal_indicator_light"_s)), 1);
    }
};

QTEST_GUILESS_MAIN(TestSignalIndicator)
#include "test_signal_indicator.moc"
