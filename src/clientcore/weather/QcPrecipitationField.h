// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"
#include "common/world/QcPrecipitationTable.h"
#include "common/world/QcWeather.h"

#include <QHash>
#include <QList>
#include <QRandomGenerator>

#include <memory>

class QcClientWeather;
class QcClientWorld;
class QcContentRegistry;

/*! \brief One particle as the precipitation renderer draws it, an instance of 16 bytes: the
    lowest point of the particle, whether it is a dot like a snowflake (else a raindrop's streak),
    its opacity, its entry in the renderer's palette, 0 for the colour of rain and snow and
    1 + QcParticleKind for an ambient particle, and how much larger than a flake a dot is, in
    quarters of a flake (decisions E-395, E-397, E-1120). \qcfields */
struct QcPrecipitationDrop
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    quint8 snow = 0;
    quint8 alpha = 0;
    quint8 palette = 0;
    quint8 grow = 0;
};
static_assert(sizeof(QcPrecipitationDrop) == 16);

/*! \brief Rain and snow around the camera (decision E-395): up to a budget of particles in a box
    around the eye, falling, each only where the sky is open above it.

    A particle starts above the top of its column (the highest cell that keeps rain out,
    QcPrecipitationTable, the same rule as the server's) and ends when it reaches that top;
    columns that are not loaded count as covered. Whether it is a raindrop or a snowflake follows
    from the climate the server sent and the height it starts at. GUI-free; drawn by the world
    renderer's QcPrecipitationRenderer, one instance (QcPrecipitationDrop) per particle: a
    raindrop as a thin streak, a snowflake as a small round dot facing the camera. */
class QcPrecipitationField
{
public:
    /*! \brief A falling particle. */
    struct Particle
    {
        /*! Its lowest point. */
        QcVec3 position;
        /*! Whether it is a snowflake (else a raindrop). */
        bool snow = false;
        /*! Sideways sway of a snowflake (radians), so flakes do not fall in step. */
        float phase = 0.0f;
    };

    /*! Particles at full strength; the shown number scales with the rain's strength. The test
        hook QTCRAFT_WEATHER_PARTICLES=<n> overrides it (measurements). */
    static constexpr int defaultBudget = 3000;
    /*! Largest budget the test hook takes. */
    static constexpr int maxBudget = 50000;
    /*! Half the width of the box around the eye and how far it reaches above and below the eye.
        \qcshared */
    static constexpr int radius = 16;
    static constexpr double reachUp = 14.0;
    static constexpr double reachDown = 10.0;
    /*! Falling speeds in metres per second and the drift of rain in the wind. \qcshared */
    static constexpr double rainSpeed = 14.0;
    static constexpr double rainDrift = 1.6;
    static constexpr double snowSpeed = 1.6;
    /*! Column tops are read again after this many seconds (blocks placed or broken under the
        rain). */
    static constexpr double columnRefreshSeconds = 0.5;
    /*! Length in seconds of the streak a raindrop draws (motion blur) and the diameter of a
        flake in metres (on screen at least two and at most a few pixels,
        QcPrecipitationRenderer). \qcshared */
    static constexpr double rainStreakSeconds = 0.04;
    static constexpr double flakeSize = 0.12;

    explicit QcPrecipitationField(int budget = defaultBudget);

    /*! Returns the budget of this run: defaultBudget or the test hook. */
    static int selectedBudget();

    /*! Advances the field by \a seconds around \a eye: particles fall, those that landed or left
        the box start again above an open column; their number follows the rain's strength in
        \a weather; \a world tells open columns. */
    void update(const QcClientWorld &world, const QcClientWeather &weather, const QcVec3 &eye, double seconds);
    /*! Returns \a particle as an instance for the renderer (QcDropPool collects them), its position
        relative to \a origin (decision E-883); raindrops and flakes grow more opaque with the \a
        strength of the rain. */
    [[nodiscard]] static QcPrecipitationDrop dropOf(const Particle &particle, float strength, const QcVec3 &origin);
    /*! Returns the streak of a raindrop: from the drop up to where it was rainStreakSeconds ago. */
    static QcVec3 rainStreak() { return {-rainDrift * rainStreakSeconds, rainSpeed * rainStreakSeconds, 0.0}; }

    /*! Returns the falling particles. */
    const QList<Particle> &particles() const { return m_particles; }
    /*! Returns the particles at full strength. */
    int budget() const { return m_budget; }
    /*! Returns the highest cell of the column (\a x, \a z) of \a world that keeps rain out
        (QcPrecipitationTable) as last read; QcWorld::worldHeight for a column that is not
        loaded. */
    int columnTop(const QcClientWorld &world, int x, int z);

private:
    // A new particle in an open column of the box around `eye`, at a random height (`anywhere`) or
    // at the top of the box; none after a few columns without open sky.
    [[nodiscard]] std::optional<Particle> spawn(const QcClientWorld &world, const QcClientWeather &weather, const QcVec3 &eye,
                                                bool anywhere);
    void ensureTable(const QcClientWorld &world);
    // Whether the particle has landed or left the box.
    bool ended(const QcClientWorld &world, const Particle &particle, const QcVec3 &eye);

    int m_budget = defaultBudget;
    QList<Particle> m_particles;
    QRandomGenerator m_random{20251015u};
    std::shared_ptr<const QcPrecipitationTable> m_table;
    const QcContentRegistry *m_tableContent = nullptr;
    QHash<quint64, int> m_columnTops;
    double m_sinceRefresh = 0.0;
    double m_time = 0.0;
};
