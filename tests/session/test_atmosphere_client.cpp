// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// The atmosphere of the biome on the client (decisions E-1120 to E-1123): the fog blend, the sky
// it gives, the particles of biomes and blocks, the biome's ambience and its music.

#include "clientcore/session/QcClientSounds.h"
#include "clientcore/session/QcWorldMirror.h"
#include "clientcore/sound/QcMusicComposer.h"
#include "clientcore/weather/QcAtmosphereParticles.h"
#include "clientcore/weather/QcClientAtmosphere.h"
#include "common/content/loader/QcBaseContent.h"
#include "common/physics/QcPhysics.h"
#include "fixtures/QcTestWorld.h"

#include <QSet>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <tuple>

using namespace Qt::StringLiterals;

namespace {

bool fuzzy(double a, double b)
{
    return std::abs(a - b) < 1e-6;
}

bool fuzzy(QVector3D a, QVector3D b)
{
    return (a - b).length() < 1e-4f;
}

// FNV-1a over 64-bit words.
quint64 mixed(quint64 hash, qint64 value)
{
    for (int i = 0; i < 8; ++i) {
        hash ^= static_cast<quint64>(value >> (8 * i)) & 0xFFu;
        hash *= 0x100000001B3ULL;
    }
    return hash;
}

// A fingerprint of a piece: every note, its root, scale, tempo and length, rounded so that the
// last bits of floating-point arithmetic do not matter.
quint64 fingerprint(const QcMusicComposer::Piece &piece)
{
    quint64 hash = 0xCBF29CE484222325ULL;
    for (const QcMusicComposer::Note &note : piece.notes) {
        hash = mixed(hash, static_cast<qint64>(note.voice));
        hash = mixed(hash, note.pitch);
        hash = mixed(hash, std::llround(note.start * 1e6));
        hash = mixed(hash, std::llround(note.duration * 1e6));
        hash = mixed(hash, std::lround(note.velocity * 1e4f));
    }
    hash = mixed(hash, piece.root);
    hash = mixed(hash, piece.minor ? 1 : 0);
    hash = mixed(hash, std::llround(piece.beatsPerMinute * 1e3));
    hash = mixed(hash, std::llround(piece.seconds * 1e6));
    return hash;
}

QcBlockPos cellOf(const QcVec3 &position)
{
    return {static_cast<int>(std::floor(position.x)), static_cast<int>(std::floor(position.y)), static_cast<int>(std::floor(position.z))};
}

} // namespace

class TestAtmosphereClient : public QObject
{
    Q_OBJECT

    std::shared_ptr<const QcContentRegistry> m_base;

    const QcContentRegistry &content() const { return *m_base; }
    QcBlockId id(const QString &name) const { return content().blockId(name).value_or(qcAirBlockId); }
    const QcBiomeAtmosphere &atmosphereOf(const QString &biome) const
    {
        const QcBiomeDefinition *found = content().worldgen().biome(biome);
        if (!found) {
            qFatal("biome missing");
        }
        return found->atmosphere;
    }

    // Lets `block` at `pos` give off its particles `samples` times.
    QcAtmosphereParticles sample(const QcTestWorld &world, QcBlockPos pos, int samples) const
    {
        QcAtmosphereParticles particles(100000);
        for (int i = 0; i < samples; ++i) {
            particles.sampleBlock(world, pos);
        }
        return particles;
    }

private slots:
    void initTestCase()
    {
        const QcResult<QcBaseContent::Loaded> base = QcBaseContent::load();
        QVERIFY(base);
        m_base = base->registry;
    }

    // Entering a biome raises the share of its fog over blendSeconds; the colour does not drift
    // from the dimension's, as the fog replaced nothing before.
    void fogBlendsIn()
    {
        QcClientAtmosphere atmosphere;
        QCOMPARE(atmosphere.fog().colourShare, 0.0);
        atmosphere.receive({atmosphereOf(u"qtcraft:nether_wastes"_s)});
        QCOMPARE(atmosphere.fog().colourShare, 0.0);
        QVERIFY(fuzzy(atmosphere.fog().colour, QcSky::colourOf(0x330808)));

        atmosphere.tick(QcClientAtmosphere::blendSeconds / 2.0);
        QVERIFY(fuzzy(atmosphere.fog().colourShare, 0.5));
        QVERIFY(fuzzy(atmosphere.fog().startShare, 0.5));
        QVERIFY(fuzzy(atmosphere.fog().start, 0.05));
        QVERIFY(fuzzy(atmosphere.fog().end, 0.5));

        atmosphere.tick(QcClientAtmosphere::blendSeconds);
        QCOMPARE(atmosphere.fog().colourShare, 1.0);
        QCOMPARE(atmosphere.fog().endShare, 1.0);
    }

    // From one biome to the next the colour itself blends; the same atmosphere again changes nothing.
    void fogBlendsBetweenBiomes()
    {
        QcClientAtmosphere atmosphere;
        atmosphere.receive({atmosphereOf(u"qtcraft:nether_wastes"_s)});
        atmosphere.tick(QcClientAtmosphere::blendSeconds);
        atmosphere.receive({atmosphereOf(u"qtcraft:basalt_deltas"_s)});
        QVERIFY(fuzzy(atmosphere.fog().colour, QcSky::colourOf(0x330808)));

        atmosphere.tick(QcClientAtmosphere::blendSeconds / 2.0);
        const QVector3D middle = (QcSky::colourOf(0x330808) + QcSky::colourOf(0x685f70)) * 0.5f;
        QVERIFY(fuzzy(atmosphere.fog().colour, middle));
        QCOMPARE(atmosphere.fog().colourShare, 1.0);

        atmosphere.receive({atmosphereOf(u"qtcraft:basalt_deltas"_s)});
        QVERIFY(fuzzy(atmosphere.fog().colour, middle));
        atmosphere.tick(QcClientAtmosphere::blendSeconds);
        QVERIFY(fuzzy(atmosphere.fog().colour, QcSky::colourOf(0x685f70)));
        QCOMPARE(atmosphere.current(), atmosphereOf(u"qtcraft:basalt_deltas"_s));
    }

    // Leaving for a biome without fog lowers the share and keeps the colour while it fades.
    void fogBlendsOut()
    {
        QcClientAtmosphere atmosphere;
        atmosphere.receive({atmosphereOf(u"qtcraft:crimson_forest"_s)});
        atmosphere.tick(QcClientAtmosphere::blendSeconds);
        atmosphere.receive({});
        atmosphere.tick(QcClientAtmosphere::blendSeconds * 0.75);
        QVERIFY(fuzzy(atmosphere.fog().colourShare, 0.25));
        QVERIFY(fuzzy(atmosphere.fog().colour, QcSky::colourOf(0x330303)));
        atmosphere.tick(QcClientAtmosphere::blendSeconds);
        QCOMPARE(atmosphere.fog().colourShare, 0.0);
        QVERIFY(atmosphere.current().isEmpty());
    }

    // A full share shows the atmosphere's colour and distances whatever the dimension's fogScale;
    // no share leaves the sky as it was.
    void skyTakesTheFog()
    {
        QcSkyState nether;
        nether.celestial = false;
        nether.horizon = {0.2f, 0.03f, 0.03f};
        nether.zenith = nether.horizon;
        nether.fogScale = 0.5;

        QcAtmosphereFog fog;
        fog.colour = QcSky::colourOf(0x1b4745);
        fog.start = 0.05;
        fog.end = 0.5;
        const QcSkyState unchanged = QcSky::withAtmosphere(nether, fog);
        QVERIFY(fuzzy(unchanged.horizon, nether.horizon));
        QCOMPARE(unchanged.fogStart, nether.fogStart);
        QCOMPARE(unchanged.fogEnd, nether.fogEnd);

        fog.colourShare = 1.0;
        fog.startShare = 1.0;
        fog.endShare = 1.0;
        const QcSkyState sky = QcSky::withAtmosphere(nether, fog);
        QVERIFY(fuzzy(sky.horizon, fog.colour));
        QVERIFY(fuzzy(sky.zenith, fog.colour));
        QVERIFY(fuzzy(sky.cloudColor, fog.colour));
        // The renderer shows start × fogScale and end × (0.5 + 0.5 × fogScale).
        QVERIFY(fuzzy(sky.fogStart * sky.fogScale, 0.05));
        QVERIFY(fuzzy(sky.fogEnd * (0.5 + 0.5 * sky.fogScale), 0.5));

        // Under an open sky the zenith stays the sky's.
        QcSkyState overworld;
        overworld.zenith = {0.3f, 0.5f, 0.9f};
        QVERIFY(fuzzy(QcSky::withAtmosphere(overworld, fog).zenith, overworld.zenith));
        QVERIFY(fuzzy(QcSky::withAtmosphere(overworld, fog).horizon, fog.colour));
    }

    // Crying obsidian cries on every face but the top, from where its neighbour leaves room; in
    // the role model one sample in five, on one of six faces.
    void cryingObsidianCriesFromItsFaces()
    {
        QcTestWorld world(content());
        const QcBlockPos obsidian{0, 64, 0};
        world.set(obsidian, id(u"qtcraft:crying_obsidian"_s));
        const QcAtmosphereParticles open = sample(world, obsidian, 3000);
        // 3000 × 0.2 × 5 / 6 = 500 expected.
        QVERIFY2(open.particles().size() > 420 && open.particles().size() < 580, qPrintable(QString::number(open.particles().size())));
        for (const QcAtmosphereParticles::Particle &tear : open.particles()) {
            QCOMPARE(tear.kind, QcParticleKind::DrippingTear);
            QVERIFY(tear.position.y < obsidian.y + 1.0);
            QVERIFY(cellOf(tear.position) != obsidian);
            QVERIFY(world.blockAt(cellOf(tear.position)) == qcAirBlockId);
        }

        // Covered on all sides but below, it only drips down.
        for (const QcBlockPos side : {QcBlockPos{1, 64, 0}, QcBlockPos{-1, 64, 0}, QcBlockPos{0, 64, 1}, QcBlockPos{0, 64, -1}}) {
            world.set(side, id(u"qtcraft:obsidian"_s));
        }
        const QcAtmosphereParticles covered = sample(world, obsidian, 3000);
        QVERIFY(!covered.particles().isEmpty());
        QVERIFY(covered.particles().size() < 200);
        for (const QcAtmosphereParticles::Particle &tear : covered.particles()) {
            QCOMPARE(cellOf(tear.position), (QcBlockPos{0, 63, 0}));
        }
    }

    // Lava soaks through a full block into a free cell below it, and nowhere else.
    void lavaDripsThroughTheCeiling()
    {
        QcTestWorld world(content());
        const QcBlockPos lava{0, 70, 0};
        world.set(lava, id(u"qtcraft:lava"_s));
        QVERIFY(sample(world, lava, 1000).particles().isEmpty());

        world.set(lava.offset(0, -1, 0), id(u"qtcraft:netherrack"_s));
        const QcAtmosphereParticles drips = sample(world, lava, 1000);
        QVERIFY2(drips.particles().size() > 60 && drips.particles().size() < 140, qPrintable(QString::number(drips.particles().size())));
        for (const QcAtmosphereParticles::Particle &drip : drips.particles()) {
            QCOMPARE(drip.kind, QcParticleKind::DrippingLava);
            QCOMPARE(cellOf(drip.position), (QcBlockPos{0, 68, 0}));
        }

        world.set(lava.offset(0, -2, 0), id(u"qtcraft:netherrack"_s));
        QVERIFY(sample(world, lava, 1000).particles().isEmpty());
    }

    // Souls rise from soul sand with room above, never from covered sand.
    void soulsRiseFromSoulSand()
    {
        QcTestWorld world(content());
        const QcBlockPos sand{0, 64, 0};
        world.set(sand, id(u"qtcraft:soul_sand"_s));
        const QcAtmosphereParticles souls = sample(world, sand, 20000);
        QVERIFY2(souls.particles().size() > 20 && souls.particles().size() < 70, qPrintable(QString::number(souls.particles().size())));
        for (const QcAtmosphereParticles::Particle &soul : souls.particles()) {
            QCOMPARE(soul.kind, QcParticleKind::Soul);
            QCOMPARE(cellOf(soul.position), (QcBlockPos{0, 65, 0}));
        }

        world.set(sand.offset(0, 1, 0), id(u"qtcraft:netherrack"_s));
        QVERIFY(sample(world, sand, 20000).particles().isEmpty());
        QVERIFY(sample(world, sand.offset(0, 1, 0), 20000).particles().isEmpty());
    }

    // A biome's particles appear only in free cells around the eye, within the budget.
    void biomeParticlesFillFreeCells()
    {
        QcTestWorld world(content());
        const QcVec3 eye{0.5, 80.5, 0.5};
        QcBiomeAtmosphere spores;
        spores.particles = {{QcParticleKind::CrimsonSpore, 0.5}};

        QcAtmosphereParticles none;
        none.spawnTick(world, {}, eye);
        QVERIFY(none.particles().isEmpty());

        QcAtmosphereParticles air(300);
        air.spawnTick(world, spores, eye);
        QCOMPARE(air.particles().size(), 300);
        for (const QcAtmosphereParticles::Particle &spore : air.particles()) {
            QCOMPARE(spore.kind, QcParticleKind::CrimsonSpore);
            QVERIFY(std::abs(spore.position.x - eye.x) <= QcAtmosphereParticles::farRadius + 1);
            QVERIFY(std::abs(spore.position.y - eye.y) <= QcAtmosphereParticles::farRadius + 1);
        }

        // Inside stone, beyond which nothing is loaded, no cell is free.
        world.loadedRadius = 3;
        world.fillBox({-3, 40, -3}, {3, 120, 3}, id(u"qtcraft:netherrack"_s));
        QcAtmosphereParticles buried;
        buried.spawnTick(world, spores, eye);
        QVERIFY(buried.particles().isEmpty());

        // Lava is no free cell either; in the role model particles float in air only.
        world.fillBox({-3, 40, -3}, {3, 120, 3}, id(u"qtcraft:lava"_s));
        QcAtmosphereParticles inLava;
        inLava.spawnTick(world, spores, eye);
        QVERIFY(std::ranges::none_of(inLava.particles(), [](const auto &p) { return p.kind == QcParticleKind::CrimsonSpore; }));
    }

    // A drop hangs for its kind's ticks, then falls and ends on the floor; ash sinks and ends with
    // its life.
    void dropsHangThenFall()
    {
        QcTestWorld world(content());
        world.set({0, 70, 0}, id(u"qtcraft:lava"_s));
        world.set({0, 69, 0}, id(u"qtcraft:netherrack"_s));
        world.fillBox({-1, 60, -1}, {1, 60, 1}, id(u"qtcraft:netherrack"_s));
        QcAtmosphereParticles particles(1);
        const QcVec3 eye{0.5, 64.0, 0.5};
        for (int i = 0; i < 100 && particles.particles().isEmpty(); ++i) {
            particles.sampleBlock(world, {0, 70, 0});
        }
        QCOMPARE(particles.particles().size(), 1);
        const double hangs = QcAtmosphereParticles::look(QcParticleKind::DrippingLava).hangTicks * QcPhysics::tickDuration;
        QCOMPARE(hangs, 2.0);
        const double startY = particles.particles().constFirst().position.y;

        const double step = QcPhysics::tickDuration;
        const long hangSteps = std::lround(hangs / step);
        for (long i = 1; i < hangSteps; ++i) {
            particles.move(world, eye, step);
        }
        QCOMPARE(particles.particles().constFirst().position.y, startY);
        int ticks = 0;
        while (!particles.particles().isEmpty() && ticks < 200) {
            const double before = particles.particles().constFirst().position.y;
            particles.move(world, eye, step);
            QVERIFY(particles.particles().isEmpty() || particles.particles().constFirst().position.y <= before);
            ++ticks;
        }
        QVERIFY(particles.particles().isEmpty());
        // About 8 m at 24 m/s² with drag: around 20 ticks.
        QVERIFY2(ticks > 10 && ticks < 40, qPrintable(QString::number(ticks)));
    }

    // Particles are drawn with the palette entry of their kind and fade in and out.
    void particlesDrawWithTheirPalette()
    {
        QcAtmosphereParticles::Particle ash;
        ash.kind = QcParticleKind::WhiteAsh;
        ash.position = {10.0, 70.0, -5.0};
        ash.life = 2.0;
        const QcPrecipitationDrop start = QcAtmosphereParticles::dropOf(ash, {9.0, 60.0, -5.0});
        QCOMPARE(start.palette, static_cast<quint8>(1 + static_cast<int>(QcParticleKind::WhiteAsh)));
        QCOMPARE(start.alpha, quint8{0});
        QCOMPARE(start.x, 1.0f);
        QCOMPARE(start.y, 10.0f);
        ash.age = 1.0;
        QCOMPARE(QcAtmosphereParticles::dropOf(ash, {}).alpha, quint8{255});
        ash.age = 2.0;
        QCOMPARE(QcAtmosphereParticles::dropOf(ash, {}).alpha, quint8{0});

        // The role model's colours where it has them.
        const QcParticleLook spore = QcAtmosphereParticles::look(QcParticleKind::CrimsonSpore);
        QCOMPARE(spore.red, 0.9f);
        QCOMPARE(spore.green, 0.4f);
        QCOMPARE(spore.blue, 0.5f);
        QCOMPARE(QcAtmosphereParticles::look(QcParticleKind::DrippingTear).hangTicks, 100);
        QCOMPARE(start.grow, quint8{0});

        // Drops are drawn 1.75 times a flake's size, hanging under their point.
        QcAtmosphereParticles::Particle tear;
        tear.kind = QcParticleKind::DrippingTear;
        tear.position = {0.5, 63.95, 0.5};
        tear.life = 10.0;
        const QcPrecipitationDrop drop = QcAtmosphereParticles::dropOf(tear, {0.0, 60.0, 0.0});
        QCOMPARE(drop.grow, quint8{3});
        QCOMPARE(drop.alpha, quint8{255});
        QVERIFY(fuzzy(drop.y, 3.95 - QcPrecipitationField::flakeSize * 1.75));
        QCOMPARE(QcAtmosphereParticles::look(QcParticleKind::DrippingLava).grow, 3);
    }

    // The biome's loop fades in at fadePerSecond; another biome's waits until the old one is gone.
    void biomeLoopCrossFades()
    {
        QcClientSounds sounds;
        const QcBiomeAtmosphere &wastes = atmosphereOf(u"qtcraft:nether_wastes"_s);
        const QcBiomeAtmosphere &valley = atmosphereOf(u"qtcraft:soul_sand_valley"_s);
        const auto silent = [](const QcSoundCue &) {};
        const int secondTicks = static_cast<int>(std::lround(1.0 / QcPhysics::tickDuration));
        quint64 tick = 0;
        const auto run = [&](const QcBiomeAtmosphere &atmosphere, int ticks) {
            for (int i = 0; i < ticks; ++i) {
                sounds.updateBiomeAmbience(atmosphere, {}, tick++, silent);
            }
        };

        run(wastes, 1);
        QCOMPARE(sounds.biomeLoop(), wastes.loop);
        run(wastes, secondTicks - 1);
        QVERIFY(std::abs(sounds.biomeLevel() - 0.5f) < 0.03f);
        run(wastes, secondTicks * 2);
        QCOMPARE(sounds.biomeLevel(), 1.0f);

        run(valley, secondTicks);
        QCOMPARE(sounds.biomeLoop(), wastes.loop);
        QVERIFY(sounds.biomeLevel() < 0.55f);
        run(valley, secondTicks + 2);
        QCOMPARE(sounds.biomeLoop(), valley.loop);
        run(valley, secondTicks * 3);
        QCOMPARE(sounds.biomeLevel(), 1.0f);

        run({}, secondTicks * 3);
        QVERIFY(sounds.biomeLoop().isEmpty());
        QCOMPARE(sounds.biomeLevel(), 0.0f);
    }

    // The biome's sound plays now and then at its chance per tick, around the listener.
    void additionsPlayByChance()
    {
        QcClientSounds sounds;
        const QcBiomeAtmosphere &deltas = atmosphereOf(u"qtcraft:basalt_deltas"_s);
        const QcVec3 eye{100.0, 70.0, -40.0};
        QList<QcSoundCue> played;
        const int ticks = 20000;
        for (int tick = 0; tick < ticks; ++tick) {
            sounds.updateBiomeAmbience(deltas, eye, static_cast<quint64>(tick), [&played](const QcSoundCue &cue) { played.append(cue); });
        }
        // 20000 × 0.0111 = 222 expected.
        QVERIFY2(played.size() > 170 && played.size() < 280, qPrintable(QString::number(played.size())));
        for (const QcSoundCue &cue : std::as_const(played)) {
            QCOMPARE(cue.key, deltas.additions);
            const QcVec3 d = cue.position - eye;
            QVERIFY(fuzzy(std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z), QcClientSounds::additionsDistance));
        }

        int plain = 0;
        for (int tick = 0; tick < ticks; ++tick) {
            sounds.updateBiomeAmbience({}, eye, static_cast<quint64>(tick), [&plain](const QcSoundCue &) { ++plain; });
        }
        QCOMPARE(plain, 0);
    }

    // The default style composes the overworld's pieces exactly as before biomes had music: the
    // fingerprints were taken from the composer without styles (dev 9218842). A change of the
    // order of the draws fails here.
    void overworldMusicStaysTheSame_data()
    {
        QTest::addColumn<quint64>("seed");
        QTest::addColumn<quint64>("fingerprint");
        QTest::addColumn<qsizetype>("notes");
        QTest::newRow("1") << quint64{1} << quint64{0x1e7469f2c1e067c3ULL} << qsizetype{42};
        QTest::newRow("42") << quint64{42} << quint64{0x36351c6a1aef3906ULL} << qsizetype{39};
        QTest::newRow("20261109") << quint64{20261109} << quint64{0x483ccc873614f24cULL} << qsizetype{43};
        QTest::newRow("deadbeef") << quint64{0xDEADBEEF} << quint64{0x8567e5142c0b8540ULL} << qsizetype{42};
    }

    void overworldMusicStaysTheSame()
    {
        QFETCH(quint64, seed);
        QFETCH(quint64, fingerprint);
        QFETCH(qsizetype, notes);
        const QcMusicComposer::Piece piece = QcMusicComposer::compose(seed);
        QCOMPARE(piece.notes.size(), notes);
        QCOMPARE(::fingerprint(piece), fingerprint);
        QCOMPARE(::fingerprint(QcMusicComposer::compose(seed, QcMusicStyle{})), fingerprint);
    }

    // The world mirror takes the atmosphere the server sends and forgets it with the dimension,
    // so the fog of the old one never blends into the new.
    void mirrorForgetsTheAtmosphereWithTheDimension()
    {
        QcWorldMirror mirror;
        const QcBiomeAtmosphere &wastes = atmosphereOf(u"qtcraft:nether_wastes"_s);
        std::ignore = mirror.handle(QcMsg::Atmosphere{wastes}, {}, 0);
        QCOMPARE(mirror.atmosphere().current(), wastes);
        for (quint64 tick = 1; tick <= 200; ++tick) {
            mirror.tick(tick);
        }
        QCOMPARE(mirror.atmosphere().fog().colourShare, 1.0);

        QcMsg::DimensionChange change{QcDimensionDefinition::defaultOverworld()};
        std::ignore = mirror.handle(change, {}, 201);
        QVERIFY(mirror.atmosphere().current().isEmpty());
        QCOMPARE(mirror.atmosphere().fog().colourShare, 0.0);
        QCOMPARE(mirror.atmosphere().fog().startShare, 0.0);
    }

    // A style bounds the root, the tempo and the scale, sets the lead and may hold a drone.
    void musicFollowsTheStyle()
    {
        const QcMusicStyle style = *atmosphereOf(u"qtcraft:soul_sand_valley"_s).music;
        QVERIFY(style.drone);
        for (quint64 seed = 1; seed <= 40; ++seed) {
            const QcMusicComposer::Piece piece = QcMusicComposer::compose(seed, style);
            QVERIFY(piece.minor);
            QVERIFY(piece.root >= style.lowestRoot && piece.root <= style.highestRoot);
            QVERIFY(piece.beatsPerMinute >= style.slowestTempo && piece.beatsPerMinute <= style.fastestTempo);
            QCOMPARE(piece.lead, style.lead);
            QCOMPARE(piece.leadLowpass, style.leadLowpass);
            const auto drones = std::ranges::count_if(piece.notes, [](const auto &note) { return note.voice == QcMusicComposer::Voice::Drone; });
            QCOMPARE(drones, 1);
            const auto drone = std::ranges::find_if(piece.notes, [](const auto &note) { return note.voice == QcMusicComposer::Voice::Drone; });
            QCOMPARE(drone->pitch, piece.root - 12);
            QCOMPARE(drone->duration, piece.seconds);
        }

        // The default style is the overworld's: either scale, its roots and tempos, no drone.
        QSet<bool> modes;
        for (quint64 seed = 1; seed <= 40; ++seed) {
            const QcMusicComposer::Piece piece = QcMusicComposer::compose(seed);
            modes.insert(piece.minor);
            QVERIFY(piece.root >= 43 && piece.root <= 49);
            QVERIFY(piece.beatsPerMinute >= 66 && piece.beatsPerMinute <= 90);
            QCOMPARE(piece.lead, QcSoundWave::Triangle);
            QVERIFY(std::ranges::none_of(piece.notes, [](const auto &note) { return note.voice == QcMusicComposer::Voice::Drone; }));
        }
        QCOMPARE(modes.size(), 2);

        // The same seed gives the same melody whatever the style's drone.
        QcMusicStyle withDrone;
        withDrone.drone = true;
        QcMusicComposer::Piece droned = QcMusicComposer::compose(9, withDrone);
        droned.notes.removeLast();
        QCOMPARE(droned.notes, QcMusicComposer::compose(9).notes);
    }
};

QTEST_GUILESS_MAIN(TestAtmosphereClient)
#include "test_atmosphere_client.moc"
