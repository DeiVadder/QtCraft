// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/weather/QcPrecipitationField.h"
#include "common/world/QcAtmosphere.h"

#include <QList>
#include <QRandomGenerator>

#include <optional>

class QcBlockAccess;

/*! \brief How a kind of ambient particle looks and moves (decision E-1120). \qcfields */
struct QcParticleLook
{
    /*! Linear colour in full light. */
    float red = 1.0f;
    float green = 1.0f;
    float blue = 1.0f;
    /*! Least light it shows in (0..1): 1 glows in the dark, 0 takes the sky's light only. */
    float selfLight = 0.0f;
    /*! Shortest and longest life in ticks, drawn evenly. */
    int shortestLife = 20;
    int longestLife = 60;
    /*! Speed at which it sinks (negative: rises) and how far it sways sideways, metres per
        second. */
    double sink = 0.0;
    double sway = 0.0;
    /*! Ticks a drop hangs under its block before it falls; 0 for particles that float. */
    int hangTicks = 0;
    /*! How much larger than a snowflake its dot is drawn, in quarters of a flake. */
    int grow = 0;
};

/*! \brief The ambient particles around the camera (decisions E-1120, E-1121): the ash and spores
    of the biome the player stands in and what blocks give off, tears under crying obsidian, lava
    soaking through the block under it, souls over soul sand.

    Spawns as the role model does: every tick it samples nearSamples cells around the eye within
    nearRadius and as many within farRadius (each axis the difference of two even draws, so cells
    near the eye are sampled more often). A sampled cell that is free gets each particle of the
    atmosphere with that particle's chance; a sampled block gives off its particles
    (QcBlockDefinition::particles) with their chances. Particles move every frame; drops hang,
    then fall until they reach a solid cell. GUI-free; drawn with the precipitation
    (QcDropPool, QcPrecipitationRenderer). */
class QcAtmosphereParticles
{
public:
    /*! \brief One particle. */
    struct Particle
    {
        /*! Where it is, its velocity in metres per second. \qcshared */
        QcVec3 position;
        QcVec3 velocity;
        /*! Its kind. */
        QcParticleKind kind = QcParticleKind::Ash;
        /*! Seconds it has lived, seconds it lives and seconds it still hangs (drops). \qcshared */
        double age = 0.0;
        double life = 1.0;
        double hang = 0.0;
        /*! Where in its sway it starts (radians), so particles do not sway in step. */
        float phase = 0.0f;
    };

    /*! Most particles at once; spawning waits while there are as many. */
    static constexpr int defaultBudget = 4000;
    /*! Cells sampled per tick near the eye and further out, and how far each reaches (each axis);
        the role model's numbers. \qcshared */
    static constexpr int nearSamples = 667;
    static constexpr int nearRadius = 16;
    static constexpr int farSamples = 667;
    static constexpr int farRadius = 32;
    /*! Particles further from the eye than this end. */
    static constexpr double keepDistance = 48.0;
    /*! Downward acceleration of a falling drop in metres per second squared and the share of its
        speed it keeps per tick (the role model's 0.06 blocks per tick squared and 0.98).
        \qcshared */
    static constexpr double dropGravity = 24.0;
    static constexpr double dropDrag = 0.98;
    /*! Most ticks of spawning made up in one update (a long frame). */
    static constexpr int maxTicksPerUpdate = 4;

    /*! An empty field with \a budget particles at most, its draws from \a seed. */
    explicit QcAtmosphereParticles(int budget = defaultBudget, quint32 seed = 20261109u);

    /*! Returns how \a kind looks and moves. */
    static QcParticleLook look(QcParticleKind kind);

    /*! Advances the field by \a seconds around \a eye in \a world: the particles move, those that
        ended go, and each tick passed spawns new ones by \a atmosphere and the blocks. */
    void update(const QcBlockAccess &world, const QcBiomeAtmosphere &atmosphere, const QcVec3 &eye, double seconds);
    /*! One tick of spawning around \a eye in \a world by \a atmosphere (update() calls it). */
    void spawnTick(const QcBlockAccess &world, const QcBiomeAtmosphere &atmosphere, const QcVec3 &eye);
    /*! Lets the block at \a pos of \a world give off its particles, as if it was sampled (tests,
        spawnTick). */
    void sampleBlock(const QcBlockAccess &world, QcBlockPos pos);
    /*! Moves the particles by \a seconds in \a world and drops those that ended, are further than
        keepDistance from \a eye or (drops) reached a solid cell. */
    void move(const QcBlockAccess &world, const QcVec3 &eye, double seconds);

    /*! Returns the particles. */
    const QList<Particle> &particles() const { return m_particles; }
    /*! Forgets every particle (another dimension or session). */
    void clear() { setParticles({}); }
    /*! Returns \a particle as an instance for the precipitation renderer, its position relative
        to \a origin: a dot of its kind's colour (palette entry 1 + kind) and size, fading in and
        out; a drop's dot hangs under its position. */
    [[nodiscard]] static QcPrecipitationDrop dropOf(const Particle &particle, const QcVec3 &origin);

private:
    void spawnIn(const QcBlockAccess &world, const QcBiomeAtmosphere &atmosphere, QcBlockPos pos);
    void giveOffAll(const QcBlockAccess &world, QcBlockId id, QcBlockPos pos);
    void giveOff(const QcBlockAccess &world, const QcParticleEmitter &emitter, QcBlockPos pos);
    void add(QcParticleKind kind, const QcVec3 &position);
    // Returns particle after seconds of drift (or of hanging and falling for drops), or
    // nothing when a falling drop reached a solid cell of world.
    [[nodiscard]] static std::optional<Particle> moved(Particle particle, const QcBlockAccess &world, double seconds);
    QRandomGenerator &random() { return m_random; }
    double unit() { return random().generateDouble(); }
    void setParticles(QList<Particle> particles) { m_particles = std::move(particles); }
    double pending() const { return m_pending; }
    void setPending(double seconds) { m_pending = seconds; }

    int m_budget = defaultBudget;
    QList<Particle> m_particles;
    QRandomGenerator m_random;
    double m_pending = 0.0;
};
