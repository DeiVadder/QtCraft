#include "clientcore/sound/QcAmbience.h"
#include "clientcore/sound/QcLocalSounds.h"
#include "QcTestWorld.h"

#include <QTest>

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
        const QcAmbience::Levels low = QcAmbience::targets(world, {0.5, 64.0, 0.5});
        QCOMPARE(low.wind, 0.3f);
        QCOMPARE(low.cave, 0.0f);
        QCOMPARE(low.water, 0.0f);
        // Halfway between 64 and 120: 0.3 + 0.7 / 2.
        QCOMPARE(QcAmbience::targets(world, {0.5, 92.0, 0.5}).wind, 0.65f);
        QCOMPARE(QcAmbience::targets(world, {0.5, 150.0, 0.5}).wind, 1.0f);
        QCOMPARE(QcAmbience::targets(world, {0.5, 20.0, 0.5}).wind, 0.3f);
    }

    void roofsMuffleTheWindAndRockMakesACave()
    {
        QcTestWorld world(m_content);
        world.set({0, 67, 0}, m_stone);
        const QcAmbience::Levels sheltered = QcAmbience::targets(world, {0.5, 64.0, 0.5});
        QCOMPARE(sheltered.wind, 0.3f * 0.3f);
        QCOMPARE(sheltered.cave, 0.0f);

        // Ten blocks of rock above: a cave, 0.5 + 0.5 * (10 - 4) / 8.
        world.fillBox({0, 70, 0}, {0, 78, 0}, m_stone);
        const QcAmbience::Levels cave = QcAmbience::targets(world, {0.5, 64.5, 0.5});
        QCOMPARE(cave.wind, 0.0f);
        QCOMPARE(cave.cave, 0.875f);

        // Water is no ceiling.
        QcTestWorld lake(m_content);
        lake.fillBox({0, 66, 0}, {0, 75, 0}, m_water);
        QCOMPARE(QcAmbience::targets(lake, {0.5, 64.5, 0.5}).cave, 0.0f);
    }

    void waterNearbyIsHeard()
    {
        QcTestWorld world(m_content);
        // Six water blocks on sampled positions (every second block) and one between them.
        for (const QcBlockPos offset : {QcBlockPos{2, -2, 0}, {4, -2, 0}, {-2, -2, 0}, {0, -2, 2}, {0, -2, 4}, {0, -2, -2}, {1, -2, 0}}) {
            world.set(QcBlockPos{0, 64, 0}.offset(offset.x, offset.y, offset.z), m_water);
        }
        QCOMPARE(QcAmbience::targets(world, {0.5, 64.5, 0.5}).water, 6.0f / QcAmbience::fullWaterBlocks);
        // Under water it is all water.
        world.set({0, 64, 0}, m_water);
        QCOMPARE(QcAmbience::targets(world, {0.5, 64.5, 0.5}).water, 1.0f);
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
        QCOMPARE(second.wind, 0.5f);
        QCOMPARE(second.cave, 0.5f);
        QCOMPARE(second.water, 0.2f);
        QVERIFY(QcAmbience::approach(second, full, 2.0) == full);
        const QcAmbience::Levels down = QcAmbience::approach(full, silent, 0.5);
        QCOMPARE(down.wind, 0.75f);
        QCOMPARE(down.water, 0.0f);
        QVERIFY(QcAmbience::approach(full, silent, -1.0) == full);
    }
};

QTEST_GUILESS_MAIN(TestSoundWorld)
#include "test_sound_world.moc"
