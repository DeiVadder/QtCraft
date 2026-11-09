// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcSoundDefinition.h"

#include <QDataStream>
#include <QList>
#include <QString>

#include <optional>

/*! \brief A kind of ambient particle the client shows (decision E-1120): the biomes' ash and
    spores, souls over soul sand and drops that hang under a block and fall. */
enum class QcParticleKind : quint8 {
    Ash, // Dark grey flakes that sink slowly ("ash").
    WhiteAsh, // Pale flakes that tumble and sink ("white_ash").
    CrimsonSpore, // Red specks that drift down ("crimson_spore").
    WarpedSpore, // Blue-green specks that drift up ("warped_spore").
    Soul, // A glowing blue soul that rises from the ground and fades ("soul").
    DrippingLava, // A glowing drop of lava that hangs, then falls ("dripping_lava").
    DrippingTear, // A glowing purple tear that hangs, then falls ("dripping_tear").
};

/*! The number of QcParticleKind values. */
inline constexpr int qcParticleKindCount = 7;
static_assert(static_cast<int>(QcParticleKind::DrippingTear) + 1 == qcParticleKindCount);

/*! \brief A particle kind with its chance per sampled cell (decision E-1120). \qcfields */
struct QcParticleChance
{
    QcParticleKind kind = QcParticleKind::Ash;
    double chance = 0.0;

    friend bool operator==(const QcParticleChance &, const QcParticleChance &) = default;
};

/*! \brief Particles a block gives off when the client samples it (a block's \c {"particles"},
    decision E-1121). */
struct QcParticleEmitter
{
    /*! Where the particle starts. */
    enum class From : quint8 {
        Top, // In the cell above, if it is free ("top").
        Bottom, // Under the block, if the cell below is free ("bottom").
        /*! On a random face other than the top, if the neighbour there is free; a drop starts
            under the block or at the side ("faces"). */
        Faces,
        /*! A liquid soaking through the full block under it: under that block, if the cell below
            it is free ("through"). */
        Through,
    };

    /*! The most emitters of a block (content and wire). */
    static constexpr int maxPerBlock = 4;

    /*! The particle. */
    QcParticleKind kind = QcParticleKind::DrippingLava;
    /*! Chance per sample of the block, 0..1. */
    double chance = 0.0;
    /*! Where it starts. */
    From from = From::Bottom;

    friend bool operator==(const QcParticleEmitter &, const QcParticleEmitter &) = default;
};

/*! \brief The background music a biome asks for (\c {"music"} of an atmosphere, decision E-1123);
    the defaults are those of the overworld's calm pieces. */
struct QcMusicStyle
{
    /*! The scale of a piece. */
    enum class Scale : quint8 {
        Any, // Major or minor, drawn per piece ("any").
        Major, // The major pentatonic ("major").
        Minor, // The minor pentatonic ("minor").
    };

    /*! The scale. */
    Scale scale = Scale::Any;
    /*! Lowest and highest MIDI note of the root (bass register), drawn per piece. \qcshared */
    int lowestRoot = 43;
    int highestRoot = 49;
    /*! Slowest and fastest tempo in beats per minute, drawn per piece. \qcshared */
    int slowestTempo = 66;
    int fastestTempo = 90;
    /*! The melody's wave and the cut-off of its low-pass in Hz. \qcshared */
    QcSoundWave lead = QcSoundWave::Triangle;
    double leadLowpass = 3000.0;
    /*! Whether a low drone holds the root under the whole piece. */
    bool drone = false;

    friend bool operator==(const QcMusicStyle &, const QcMusicStyle &) = default;
};

/*! \brief What the air of a biome looks and sounds like (a biome's \c {"atmosphere"}, decision
    E-1120): its fog, its ambient particles, its ambience loop with sounds now and then, and its
    music. The server sends the one where a player stands (QcMsg::Atmosphere); an empty one keeps
    the dimension's fog and the usual ambience. */
struct QcBiomeAtmosphere
{
    /*! The most particle kinds and the longest sound name. \qcshared */
    static constexpr int maxParticles = 4;
    static constexpr qsizetype maxSoundName = 128;

    /*! The fog colour, 0xRRGGBB; none keeps the dimension's. */
    std::optional<quint32> fogColour;
    /*! Where the fog starts and where it hides everything, as shares of the view distance; none
        keeps the dimension's. \qcshared */
    std::optional<double> fogStart;
    std::optional<double> fogEnd;
    /*! The particles in the free cells around the player, at most maxParticles. */
    QList<QcParticleChance> particles;
    /*! The ambience loop (a sound name); empty for the usual wind and cave loops. */
    QString loop;
    /*! A sound played now and then, with its chance per tick. \qcshared */
    QString additions;
    double additionsChance = 0.0;
    /*! The music; none keeps the default style. */
    std::optional<QcMusicStyle> music;

    /*! Returns whether the atmosphere changes nothing. */
    bool isEmpty() const { return *this == QcBiomeAtmosphere{}; }

    friend bool operator==(const QcBiomeAtmosphere &, const QcBiomeAtmosphere &) = default;
};

/*! \brief Names, limits and the wire format of particles and atmospheres (decisions E-1120,
    E-1121). */
namespace QcAtmosphere {

/*! Returns the content name of \a kind ("white_ash"). */
QString name(QcParticleKind kind);
/*! Returns the kind named \a name; std::nullopt for other names. */
[[nodiscard]] std::optional<QcParticleKind> particleKind(QStringView name);
/*! Returns where an emitter named \a name starts ("top", "bottom", "faces", "through"). */
[[nodiscard]] std::optional<QcParticleEmitter::From> emitterFrom(QStringView name);

/*! Writes \a emitters (a block's particles) to \a stream. */
void writeEmitters(QDataStream &stream, const QList<QcParticleEmitter> &emitters);
/*! Returns the emitters read from \a stream; std::nullopt (and a corrupt stream) for values the
    parser would refuse. */
[[nodiscard]] std::optional<QList<QcParticleEmitter>> readEmitters(QDataStream &stream);
/*! Writes \a atmosphere to \a stream. */
void write(QDataStream &stream, const QcBiomeAtmosphere &atmosphere);
/*! Returns the atmosphere read from \a stream; std::nullopt (and a corrupt stream) for values the
    parser would refuse. */
[[nodiscard]] std::optional<QcBiomeAtmosphere> read(QDataStream &stream);

} // namespace QcAtmosphere
