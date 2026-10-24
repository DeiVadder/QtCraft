#include "clientcore/sound/QcAmbience.h"
#include "clientcore/sound/QcLocalSounds.h"
#include "QcTestWorld.h"

#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

QcContentRegistry makeContent()
{
    QcContentRegistry content;
    const auto add = [&content](const QString &name, const QString &material, bool liquid) {
        QcBlockDefinition block;
        block.name = name;
        block.faceTextures.fill(name);
        block.soundMaterial = material;
        if (liquid) {
            block.renderLayer = QcRenderLayer::Translucent;
            block.solid = false;
            block.opaque = false;
            block.liquid = true;
        }
        if (!content.registerBlock(block)) {
            // Builds content for a member initialiser, outside any test case; no case can run without it.
            qFatal("cannot register %s", qPrintable(name));
        }
    };

    add(u"t:stone"_s, u"qtcraft:stone"_s, false);
    add(u"t:sand"_s, u"qtcraft:sand"_s, false);
    add(u"t:water"_s, QString(), true);

    QcBlockDefinition lava;
    lava.name = u"t:lava"_s;
    lava.faceTextures.fill(lava.name);
    lava.solid = false;
    lava.opaque = false;
    lava.liquid = true;
    lava.lightLevel = 15;
    if (!content.registerBlock(lava)) {
        // Builds content for a member initialiser, outside any test case; no case can run without it.
        qFatal("cannot register t:lava");
    }
    return content;
}

QcEntityMotion standing(double x, double y, double z)
{
    QcEntityMotion motion;
    motion.position = {x, y, z};
    motion.onGround = true;
    return motion;
}

bool nearly(float value, float expected)
{
    return std::abs(value - expected) < 1e-5f;
}

// Forest crowns over x 40..120 (about half the columns, fixed pseudo-random), a rock tunnel over
// x 160..220 and a lake beside the track over x 230..270; open land elsewhere.
void buildCartRoute(QcTestWorld &world)
{
    const QcBlockId stone = *world.content().blockId(u"t:stone"_s);
    const QcBlockId water = *world.content().blockId(u"t:water"_s);
    for (int x = 40; x <= 120; ++x) {
        for (int z = -6; z <= 6; ++z) {
            const quint32 hash = (static_cast<quint32>(x) * 73856093u) ^ (static_cast<quint32>(z) * 19349663u);
            if (hash % 100 < 50) {
                world.fillBox({x, 68, z}, {x, 69, z}, stone);
            }
        }
    }
    world.fillBox({160, 66, -6}, {220, 80, 6}, stone);
    world.fillBox({230, 62, 2}, {270, 62, 8}, water);
}

} // namespace

// Client-side sounds of the player's own movement and the choice of ambience loops.
class TestSoundWorld : public QObject
{
    Q_OBJECT

private:
    QcContentRegistry m_content = makeContent();
    QcBlockId m_stone = *m_content.blockId(u"t:stone"_s);
    QcBlockId m_sand = *m_content.blockId(u"t:sand"_s);
    QcBlockId m_water = *m_content.blockId(u"t:water"_s);

private slots:
    void walkingStepsOnTheGroundUnderfoot()
    {
        QcTestWorld world(m_content);
        world.fillBox({-20, 63, -2}, {0, 63, 2}, m_stone);
        world.fillBox({1, 63, -2}, {20, 63, 2}, m_sand);
        QcLocalSounds sounds;

        // 0.25 m per tick: the stride of 1.7 m is complete after seven ticks.
        QcEntityMotion before = standing(-5.0, 64.0, 0.5);
        QList<QcSoundCue> cues;
        int tick = 0;
        while (cues.isEmpty() && tick < 20) {
            QcEntityMotion after = before;
            after.position.x += 0.25;
            cues = sounds.update(before, after, false, world);
            before = after;
            ++tick;
        }

        QCOMPARE(tick, 7);
        QCOMPARE(cues.size(), 1);
        QCOMPARE(cues[0].key, u"qtcraft:block.stone.step"_s);
        QCOMPARE(cues[0].volume, QcLocalSounds::stepVolume);
        QCOMPARE(cues[0].position, before.position);
        QCOMPARE(cues[0].origin, QcSoundCue::Origin::Local);

        // On the sand further east the steps sound like sand, and sneaking is quieter.
        QList<QcSoundCue> sandCues;
        before = standing(1.5, 64.0, 0.5);
        for (int i = 0; i < 7; ++i) {
            QcEntityMotion after = before;
            after.position.x += 0.25;
            sandCues += sounds.update(before, after, true, world);
            before = after;
        }
        QCOMPARE(sandCues.size(), 1);
        QCOMPARE(sandCues[0].key, u"qtcraft:block.sand.step"_s);
        QCOMPARE(sandCues[0].volume, QcLocalSounds::sneakStepVolume);

        // Standing still makes no sound, and neither does walking where nothing is loaded.
        QVERIFY(sounds.update(before, before, false, world).isEmpty());

        world.loadedRadius = 0;
        QcLocalSounds unloaded;
        QcEntityMotion edge = standing(5.0, 64.0, 5.0);
        for (int i = 0; i < 10; ++i) {
            QcEntityMotion after = edge;
            after.position.x += 0.25;
            QVERIFY(unloaded.update(edge, after, false, world).isEmpty());
            edge = after;
        }
    }

    void jumpsAndLandings()
    {
        QcTestWorld world(m_content);
        world.fillBox({-5, 63, -5}, {5, 63, 5}, m_stone);
        QcLocalSounds sounds;
        const QcEntityMotion ground = standing(0.5, 64.0, 0.5);
        QcEntityMotion rising = ground;
        rising.onGround = false;
        rising.position.y = 64.4;
        rising.velocity.y = 8.0;
        const QList<QcSoundCue> jump = sounds.update(ground, rising, false, world);
        QCOMPARE(jump.size(), 1);
        QCOMPARE(jump[0].key, u"qtcraft:block.stone.step"_s);
        QCOMPARE(jump[0].volume, QcLocalSounds::jumpVolume);
        QCOMPARE(jump[0].pitch, 1.1f);

        // A short hop lands like a step.
        QcEntityMotion falling = rising;
        falling.velocity.y = -3.0;
        const QList<QcSoundCue> soft = sounds.update(falling, ground, false, world);
        QCOMPARE(soft.size(), 1);
        QCOMPARE(soft[0].volume, QcLocalSounds::stepVolume);
        QCOMPARE(soft[0].pitch, 1.0f);

        // A fall at 12 m/s lands louder (12 / 15) and lower.
        falling.velocity.y = -12.0;
        const QList<QcSoundCue> hard = sounds.update(falling, ground, false, world);
        QCOMPARE(hard.size(), 1);
        QCOMPARE(hard[0].volume, 0.8f);
        QCOMPARE(hard[0].pitch, 0.85f);

        // Even a very fast fall stays at full volume.
        falling.velocity.y = -40.0;
        QCOMPARE(sounds.update(falling, ground, false, world).constFirst().volume, 1.0f);
    }

    void waterSplashesAndSwimming()
    {
        QcTestWorld world(m_content);
        QcLocalSounds sounds;
        QcEntityMotion air;
        air.position = {0.5, 64.0, 0.5};
        air.velocity.y = -6.0;
        QcEntityMotion wet = air;
        wet.inWater = true;
        wet.position.y = 63.7;

        const QList<QcSoundCue> splash = sounds.update(air, wet, false, world);
        QCOMPARE(splash.size(), 1);
        QCOMPARE(splash[0].key, u"qtcraft:water.splash"_s);
        QCOMPARE(splash[0].volume, 0.5f);

        // Slipping into the water slowly makes no splash; swimming 5 m makes two strokes of 2.2 m.
        air.velocity.y = -1.0;
        QVERIFY(sounds.update(air, wet, false, world).isEmpty());

        QcLocalSounds swimmer;
        int strokes = 0;
        QcEntityMotion before = wet;
        for (int i = 0; i < 10; ++i) {
            QcEntityMotion after = before;
            after.position.z -= 0.5;
            const QList<QcSoundCue> cues = swimmer.update(before, after, false, world);
            for (const QcSoundCue &cue : cues) {
                QCOMPARE(cue.key, u"qtcraft:water.swim"_s);
                ++strokes;
            }
            before = after;
        }
        QCOMPARE(strokes, 2);
    }

    void windGrowsWithHeightUnderTheOpenSky()
    {
        QcTestWorld world(m_content);
        const auto calm = static_cast<float>(QcAmbience::calmWind);
        const QcAmbience::Levels low = QcAmbience::targets(world, {0.5, 64.0, 0.5});
        // A faint breeze on calm ground, not a constant rush.
        QVERIFY(nearly(low.wind, calm));
        QVERIFY(low.wind < 0.2f);
        QCOMPARE(low.cave, 0.0f);
        QCOMPARE(low.water, 0.0f);
        // Halfway between 64 and 120.
        QVERIFY(nearly(QcAmbience::targets(world, {0.5, 92.0, 0.5}).wind, calm + (1.0f - calm) / 2.0f));
        QVERIFY(nearly(QcAmbience::targets(world, {0.5, 150.0, 0.5}).wind, 1.0f));
        QVERIFY(nearly(QcAmbience::targets(world, {0.5, 20.0, 0.5}).wind, calm));
        // A storm raises it.
        QVERIFY(nearly(QcAmbience::targets(world, {0.5, 64.0, 0.5}, 1.0).wind, calm + static_cast<float>(QcAmbience::rainWind)));
    }

    void roofsMuffleTheWindAndRockMakesACave()
    {
        QcTestWorld world(m_content);
        // A roof over every sampled column.
        world.fillBox({-2, 67, -2}, {2, 67, 2}, m_stone);
        const QcAmbience::Levels sheltered = QcAmbience::targets(world, {0.5, 64.0, 0.5});
        QVERIFY(nearly(sheltered.wind, static_cast<float>(QcAmbience::calmWind * QcAmbience::windUnderRoof)));
        QCOMPARE(sheltered.cave, 0.0f);

        // Ten blocks of rock above: a cave, 0.5 + 0.5 * (10 - 4) / 8.
        world.fillBox({-2, 70, -2}, {2, 78, 2}, m_stone);
        const QcAmbience::Levels cave = QcAmbience::targets(world, {0.5, 64.5, 0.5});
        QCOMPARE(cave.wind, 0.0f);
        QVERIFY(nearly(cave.cave, 0.875f));

        // Water is no ceiling.
        QcTestWorld lake(m_content);
        lake.fillBox({-2, 66, -2}, {2, 75, 2}, m_water);
        QCOMPARE(QcAmbience::targets(lake, {0.5, 64.5, 0.5}).cave, 0.0f);
    }

    void aSingleTreeOnlyDampsTheWindALittle()
    {
        // One crown above the head shelters one of the nine sampled columns.
        QcTestWorld world(m_content);
        world.set({0, 67, 0}, m_stone);
        const auto calm = static_cast<float>(QcAmbience::calmWind);
        const float wind = QcAmbience::targets(world, {0.5, 64.0, 0.5}).wind;
        QVERIFY(nearly(wind, (8.0f * calm + calm * static_cast<float>(QcAmbience::windUnderRoof)) / 9.0f));
    }

    void waterNearbyIsHeard()
    {
        QcTestWorld world(m_content);
        // Six water blocks on sampled positions (every second block) and one between them.
        const QList<QcBlockPos> sampled{{2, -2, 0}, {4, -2, 0}, {-2, -2, 0}, {0, -2, 2}, {0, -2, 4}, {0, -2, -2}};
        for (const QcBlockPos offset : sampled) {
            world.set(QcBlockPos{0, 64, 0}.offset(offset.x, offset.y, offset.z), m_water);
        }
        world.set({1, 62, 0}, m_water);

        // Nearer blocks count more.
        const QcVec3 eye{0.5, 64.5, 0.5};
        double expected = 0.0;
        for (const QcBlockPos offset : sampled) {
            const QcVec3 centre{offset.x + 0.5, 64 + offset.y + 0.5, offset.z + 0.5};
            expected += 1.0 - (centre - eye).length() / QcAmbience::waterFadeDistance;
        }
        QVERIFY(std::abs(QcAmbience::targets(world, eye).water - static_cast<float>(expected / QcAmbience::fullWaterBlocks)) < 1e-5f);
        // Under water it is all water.
        world.set({0, 64, 0}, m_water);
        QCOMPARE(QcAmbience::targets(world, eye).water, 1.0f);
    }

    void waterAllAroundIsOneCappedLevel()
    {
        QcTestWorld world(m_content);
        world.fillBox({-8, 58, -8}, {8, 63, 8}, m_water);
        QCOMPARE(QcAmbience::targets(world, {0.5, 65.5, 0.5}).water, 1.0f);
    }

    void theWaterSampleStaysPutWhileWalking()
    {
        // Water only in every second row: a sample moving with the listener would hear it from one
        // block and miss it from the next.
        QcTestWorld world(m_content);
        for (int x = -20; x <= 20; x += 2) {
            world.fillBox({x, 62, -10}, {x, 62, 10}, m_water);
        }
        const float here = QcAmbience::targets(world, {0.5, 64.5, 0.5}).water;
        const float next = QcAmbience::targets(world, {1.5, 64.5, 0.5}).water;
        QVERIFY(here > 0.3f);
        QVERIFY2(std::abs(here - next) < 0.05f, qPrintable(u"%1 vs %2"_s.arg(here).arg(next)));
    }

    void lavaPopsInsteadOfSoundingLikeWater()
    {
        QcTestWorld world(m_content);
        const QcBlockId lava = *m_content.blockId(u"t:lava"_s);
        QCOMPARE(QcAmbience::nearestGlowingLiquid(world, {0.5, 64.5, 0.5}), std::nullopt);
        // Lava on two sampled positions: no water loop, and the nearer one pops.
        world.set({4, 62, 0}, lava);
        world.set({2, 62, 0}, lava);
        QCOMPARE(QcAmbience::targets(world, {0.5, 64.5, 0.5}).water, 0.0f);
        QCOMPARE(QcAmbience::nearestGlowingLiquid(world, {0.5, 64.5, 0.5}), std::optional<QcBlockPos>(QcBlockPos{2, 62, 0}));
        // Water is not lava.
        world.set({0, 62, 0}, m_water);
        QCOMPARE(QcAmbience::nearestGlowingLiquid(world, {0.5, 64.5, 0.5}), std::optional<QcBlockPos>(QcBlockPos{2, 62, 0}));
    }

    void levelsFadeGradually()
    {
        const QcAmbience::Levels silent;
        const QcAmbience::Levels full{1.0f, 1.0f, 0.2f};
        const QcAmbience::Levels second = QcAmbience::approach(silent, full, 1.0);
        // Large changes at the fade rate, small ones eased: 1 - exp(-1) of the way.
        QCOMPARE(second.wind, 0.5f);
        QCOMPARE(second.cave, 0.5f);
        QVERIFY(std::abs(second.water - 0.2f * (1.0f - std::exp(-1.0f))) < 1e-5f);

        QcAmbience::Levels levels = second;
        for (int tick = 0; tick < 200; ++tick) {
            levels = QcAmbience::approach(levels, full, 0.05);
        }
        QVERIFY(levels == full);
        const QcAmbience::Levels down = QcAmbience::approach(full, silent, 0.5);
        QCOMPARE(down.wind, 0.75f);
        QVERIFY(down.water < 0.2f && down.water > 0.0f);
        QVERIFY(QcAmbience::approach(full, silent, -1.0) == full);
    }

    // A cart at 8 m/s on a hill top, where the wind is strong, under a line of trees along the
    // track (a crown every 3 blocks): no swings as each tree passes. A single column above the
    // head would be under a crown at every third sample.
    void windStaysSteadyAlongALineOfTrees()
    {
        QcTestWorld world(m_content);
        for (int x = 0; x <= 210; x += 3) {
            world.fillBox({x, 104, 0}, {x, 105, 1}, m_stone);
        }

        QcAmbience::Levels target;
        QcAmbience::Levels levels = QcAmbience::targets(world, {0.0, 100.5, 0.5});
        float low = 1.0f;
        float high = 0.0f;
        for (int i = 0; i * 0.4 < 200.0; ++i) {
            const QcVec3 eye{i * 0.4, 100.5, 0.5};
            if (i % 10 == 0) {
                target = QcAmbience::targets(world, eye);
            }
            levels = QcAmbience::approach(levels, target, 0.05);
            if (i * 0.4 > 20.0) {
                low = std::min(low, levels.wind);
                high = std::max(high, levels.wind);
            }
        }
        qInfo() << "wind along the trees" << low << ".." << high;
        QVERIFY(low > 0.4f);
        QVERIFY2(high - low < 0.05f, qPrintable(u"wind %1..%2"_s.arg(low).arg(high)));
    }

    // A minecart at 8 m/s through open land, a forest, a rock tunnel and along a lake: the levels
    // move gently, without pulsing as single trees and gaps pass by.
    void levelsStayCalmAtCartSpeed()
    {
        QcTestWorld world(m_content);
        buildCartRoute(world);

        constexpr double speed = 8.0;
        constexpr int ticksPerSample = 10;
        constexpr double tick = 0.05;
        const float maxStep = static_cast<float>(QcAmbience::fadePerSecond * tick) + 1e-6f;
        QcAmbience::Levels target;
        QcAmbience::Levels levels;
        float forestLow = 1.0f;
        float forestHigh = 0.0f;
        float tunnelCave = 0.0f;
        for (int i = 0; i * speed * tick < 300.0; ++i) {
            const QcVec3 eye{i * speed * tick, 64.5, 0.5};
            if (i % ticksPerSample == 0) {
                target = QcAmbience::targets(world, eye);
            }
            const QcAmbience::Levels next = QcAmbience::approach(levels, target, tick);
            QVERIFY(std::abs(next.wind - levels.wind) <= maxStep);
            QVERIFY(std::abs(next.cave - levels.cave) <= maxStep);
            QVERIFY(std::abs(next.water - levels.water) <= maxStep);
            levels = next;

            // Inside the forest, after the levels had time to settle.
            if (eye.x > 60.0 && eye.x < 115.0) {
                forestLow = std::min(forestLow, levels.wind);
                forestHigh = std::max(forestHigh, levels.wind);
            }
            if (eye.x > 190.0 && eye.x < 215.0) {
                tunnelCave = std::max(tunnelCave, levels.cave);
            }
        }

        qInfo() << "forest wind" << forestLow << ".." << forestHigh << "tunnel cave" << tunnelCave;
        QVERIFY2(forestHigh - forestLow < 0.04f, qPrintable(u"wind %1..%2"_s.arg(forestLow).arg(forestHigh)));
        QVERIFY(forestHigh < static_cast<float>(QcAmbience::calmWind));
        QVERIFY(tunnelCave > 0.5f);
    }
};

QTEST_GUILESS_MAIN(TestSoundWorld)
#include "test_sound_world.moc"
