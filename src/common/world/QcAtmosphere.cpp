// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/world/QcAtmosphere.h"

#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr auto kindNames = std::to_array<QStringView>({u"ash", u"white_ash", u"crimson_spore", u"warped_spore", u"soul", u"dripping_lava",
                                                      u"dripping_tear"});
static_assert(kindNames.size() == qcParticleKindCount);
constexpr auto fromNames = std::to_array<QStringView>({u"top", u"bottom", u"faces", u"through"});
static_assert(fromNames.size() == static_cast<std::size_t>(QcParticleEmitter::From::Through) + 1);

constexpr quint8 flagFogColour = 1;
constexpr quint8 flagFogStart = 2;
constexpr quint8 flagFogEnd = 4;
constexpr quint8 flagMusic = 8;
constexpr quint8 knownFlags = 15;
constexpr quint32 maxColour = 0xFFFFFF;

bool isShare(double value)
{
    // Written as a range check that NaN fails.
    return value >= 0.0 && value <= 1.0;
}

bool plausible(const QcMusicStyle &music)
{
    return music.lowestRoot >= 0
           && music.lowestRoot <= music.highestRoot && music.highestRoot <= 127 && music.slowestTempo >= 1
           && music.slowestTempo <= music.fastestTempo && music.fastestTempo <= 400 && music.leadLowpass >= 0.0
           && music.leadLowpass <= 20000.0;
}

bool plausible(const QcBiomeAtmosphere &atmosphere)
{
    const bool fog = (!atmosphere.fogColour || *atmosphere.fogColour <= maxColour) && (!atmosphere.fogStart || isShare(*atmosphere.fogStart))
                     && (!atmosphere.fogEnd || isShare(*atmosphere.fogEnd))
                     && (!atmosphere.fogStart || !atmosphere.fogEnd || *atmosphere.fogStart < *atmosphere.fogEnd);
    const bool particles = atmosphere.particles.size() <= QcBiomeAtmosphere::maxParticles
                           && std::ranges::all_of(atmosphere.particles, [](QcParticleChance particle) { return isShare(particle.chance); });
    const bool sounds = atmosphere.loop.size() <= QcBiomeAtmosphere::maxSoundName
                        && atmosphere.additions.size() <= QcBiomeAtmosphere::maxSoundName && isShare(atmosphere.additionsChance);
    return fog && particles && sounds && (!atmosphere.music || plausible(*atmosphere.music));
}

quint8 flagsOf(const QcBiomeAtmosphere &atmosphere)
{
    return static_cast<quint8>((atmosphere.fogColour ? flagFogColour : 0) | (atmosphere.fogStart ? flagFogStart : 0)
                               | (atmosphere.fogEnd ? flagFogEnd : 0) | (atmosphere.music ? flagMusic : 0));
}

void writeMusic(QDataStream &stream, const QcMusicStyle &music)
{
    stream << static_cast<quint8>(music.scale) << static_cast<qint16>(music.lowestRoot) << static_cast<qint16>(music.highestRoot)
           << static_cast<qint16>(music.slowestTempo) << static_cast<qint16>(music.fastestTempo) << static_cast<quint8>(music.lead)
           << music.leadLowpass << music.drone;
}

// The music style of a stream; none for an unknown scale or wave.
std::optional<QcMusicStyle> readMusic(QDataStream &stream)
{
    QcMusicStyle music;
    quint8 scale = 0;
    qint16 lowestRoot = 0;
    qint16 highestRoot = 0;
    qint16 slowestTempo = 0;
    qint16 fastestTempo = 0;
    quint8 lead = 0;
    stream >> scale >> lowestRoot >> highestRoot >> slowestTempo >> fastestTempo >> lead >> music.leadLowpass >> music.drone;
    if (scale > static_cast<quint8>(QcMusicStyle::Scale::Minor) || lead > static_cast<quint8>(QcSoundWave::Saw)) {
        return std::nullopt;
    }
    music.scale = static_cast<QcMusicStyle::Scale>(scale);
    music.lowestRoot = lowestRoot;
    music.highestRoot = highestRoot;
    music.slowestTempo = slowestTempo;
    music.fastestTempo = fastestTempo;
    music.lead = static_cast<QcSoundWave>(lead);
    return music;
}

// The particle kinds of a stream: an unknown one fails.
std::optional<QcParticleKind> kindOf(quint8 value)
{
    return value < qcParticleKindCount ? std::optional(static_cast<QcParticleKind>(value)) : std::nullopt;
}

} // namespace

namespace QcAtmosphere {

QString name(QcParticleKind kind)
{
    return kindNames[static_cast<std::size_t>(kind)].toString();
}

std::optional<QcParticleKind> particleKind(QStringView name)
{
    const auto found = std::ranges::find(kindNames, name);
    return found != kindNames.end() ? std::optional(static_cast<QcParticleKind>(found - kindNames.begin())) : std::nullopt;
}

std::optional<QcParticleEmitter::From> emitterFrom(QStringView name)
{
    const auto found = std::ranges::find(fromNames, name);
    return found != fromNames.end() ? std::optional(static_cast<QcParticleEmitter::From>(found - fromNames.begin())) : std::nullopt;
}

void writeEmitters(QDataStream &stream, const QList<QcParticleEmitter> &emitters)
{
    stream << static_cast<quint32>(emitters.size());
    for (const QcParticleEmitter &emitter : emitters) {
        stream << static_cast<quint8>(emitter.kind) << emitter.chance << static_cast<quint8>(emitter.from);
    }
}

std::optional<QList<QcParticleEmitter>> readEmitters(QDataStream &stream)
{
    quint32 count = 0;
    stream >> count;
    if (count > static_cast<quint32>(QcParticleEmitter::maxPerBlock)) {
        stream.setStatus(QDataStream::ReadCorruptData);
        return std::nullopt;
    }

    QList<QcParticleEmitter> emitters;
    for (quint32 i = 0; i < count; ++i) {
        quint8 kind = 0;
        QcParticleEmitter emitter;
        quint8 from = 0;
        stream >> kind >> emitter.chance >> from;
        const std::optional<QcParticleKind> known = kindOf(kind);
        if (!known || from > static_cast<quint8>(QcParticleEmitter::From::Through) || !isShare(emitter.chance)) {
            stream.setStatus(QDataStream::ReadCorruptData);
            return std::nullopt;
        }
        emitter.kind = *known;
        emitter.from = static_cast<QcParticleEmitter::From>(from);
        emitters.append(emitter);
    }
    return emitters;
}

void write(QDataStream &stream, const QcBiomeAtmosphere &atmosphere)
{
    stream << flagsOf(atmosphere) << atmosphere.fogColour.value_or(0) << atmosphere.fogStart.value_or(0.0) << atmosphere.fogEnd.value_or(0.0);
    stream << static_cast<quint8>(atmosphere.particles.size());
    for (const QcParticleChance &particle : atmosphere.particles) {
        stream << static_cast<quint8>(particle.kind) << particle.chance;
    }
    stream << atmosphere.loop << atmosphere.additions << atmosphere.additionsChance;
    writeMusic(stream, atmosphere.music.value_or(QcMusicStyle{}));
}

std::optional<QcBiomeAtmosphere> read(QDataStream &stream)
{
    QcBiomeAtmosphere atmosphere;
    quint8 flags = 0;
    quint32 colour = 0;
    double start = 0.0;
    double end = 0.0;
    quint8 particles = 0;
    stream >> flags >> colour >> start >> end >> particles;
    bool known = (flags & ~knownFlags) == 0 && particles <= QcBiomeAtmosphere::maxParticles;
    for (quint8 i = 0; known && i < particles; ++i) {
        quint8 kind = 0;
        double chance = 0.0;
        stream >> kind >> chance;
        const std::optional<QcParticleKind> particle = kindOf(kind);
        known = particle.has_value();
        atmosphere.particles.append({particle.value_or(QcParticleKind::Ash), chance});
    }
    stream >> atmosphere.loop >> atmosphere.additions >> atmosphere.additionsChance;
    const std::optional<QcMusicStyle> music = readMusic(stream);
    known = known && music.has_value();

    atmosphere.fogColour = (flags & flagFogColour) != 0 ? std::optional(colour) : std::nullopt;
    atmosphere.fogStart = (flags & flagFogStart) != 0 ? std::optional(start) : std::nullopt;
    atmosphere.fogEnd = (flags & flagFogEnd) != 0 ? std::optional(end) : std::nullopt;
    atmosphere.music = (flags & flagMusic) != 0 ? music : std::nullopt;
    if (!known || stream.status() != QDataStream::Ok || !plausible(atmosphere)) {
        stream.setStatus(QDataStream::ReadCorruptData);
        return std::nullopt;
    }
    return atmosphere;
}

} // namespace QcAtmosphere
