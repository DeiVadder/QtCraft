// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/worldgen/QcAtmosphereParser.h"

#include "common/content/registry/QcJsonFieldReader.h"

#include <QJsonArray>

#include <array>

using namespace Qt::StringLiterals;

namespace {

// A particle kind at `key` of `reader`; a missing or unknown one fails.
std::optional<QcParticleKind> kindAt(QcJsonFieldReader &reader, const QString &key)
{
    const QString name = reader.string(key);
    const std::optional<QcParticleKind> kind = QcAtmosphere::particleKind(name);
    if (!kind) {
        reader.fail(u"unknown particle '%1'"_s.arg(name));
    }
    return kind;
}

// Reads the array at `key` of `json` entry by entry with `read(object, entryReader)`, at most
// `limit` entries; a problem in an entry stops it.
template<typename Read>
void readEntries(const QJsonValue &value, QcJsonFieldReader &reader, qsizetype limit, Read read)
{
    if (value.isUndefined()) {
        return;
    }
    if (!value.isArray() || value.toArray().size() > limit) {
        reader.fail(u"'particles' must be an array of at most %1 entries"_s.arg(limit));
        return;
    }

    const QJsonArray entries = value.toArray();
    for (const auto &entry : entries) {
        const QJsonObject object = entry.toObject();
        QcJsonFieldReader nested{object, reader.context + u" particles"_s, {}};
        if (!entry.isObject()) {
            nested.fail(u"entries must be objects"_s);
        } else {
            read(nested);
        }
        reader.adopt(nested);
        if (!reader.ok()) {
            return;
        }
    }
}

std::optional<QcMusicStyle> parseMusic(QcJsonFieldReader &reader)
{
    using namespace QcAtmosphereParser;
    static constexpr std::array<QStringView, 3> scales = {u"any", u"major", u"minor"};
    static constexpr std::array<QStringView, 4> waves = {u"sine", u"square", u"triangle", u"saw"};

    reader.warnUnknownKeys({u"scale", u"lead", u"lowestRoot", u"highestRoot", u"slowestTempo", u"fastestTempo", u"leadLowpass", u"drone"});
    QcMusicStyle music;
    const QString scale = reader.string(u"scale"_s, u"any"_s);
    const auto scaleAt = std::ranges::find(scales, scale);
    const QString lead = reader.string(u"lead"_s, u"triangle"_s);
    const auto waveAt = std::ranges::find(waves, lead);
    if (scaleAt == scales.end() || waveAt == waves.end()) {
        reader.fail(u"unknown scale '%1' or lead '%2'"_s.arg(scale, lead));
        return std::nullopt;
    }
    music.scale = static_cast<QcMusicStyle::Scale>(scaleAt - scales.begin());
    music.lead = static_cast<QcSoundWave>(waveAt - waves.begin());

    music.lowestRoot = reader.integer(u"lowestRoot"_s, music.lowestRoot, lowestRoot, highestRoot);
    music.highestRoot = reader.integer(u"highestRoot"_s, std::max(music.highestRoot, music.lowestRoot), music.lowestRoot, highestRoot);
    music.slowestTempo = reader.integer(u"slowestTempo"_s, music.slowestTempo, slowestTempo, fastestTempo);
    music.fastestTempo = reader.integer(u"fastestTempo"_s, std::max(music.fastestTempo, music.slowestTempo), music.slowestTempo, fastestTempo);
    music.leadLowpass = reader.number(u"leadLowpass"_s, music.leadLowpass, lowestLowpass, highestLowpass);
    music.drone = reader.boolean(u"drone"_s, false);
    return music;
}

// The fog of an atmosphere: colour, start and end, each none where the object names nothing.
struct Fog
{
    std::optional<quint32> colour;
    std::optional<double> start;
    std::optional<double> end;
};

Fog readFog(QcJsonFieldReader &reader)
{
    reader.warnUnknownKeys({u"colour", u"start", u"end"});
    Fog fog;
    fog.colour = reader.colour(u"colour"_s);
    if (reader.json.contains(u"start"_s)) {
        fog.start = reader.number(u"start"_s, 0.0, 0.0, 1.0);
    }
    if (reader.json.contains(u"end"_s)) {
        fog.end = reader.number(u"end"_s, 1.0, 0.01, 1.0);
    }
    if (fog.start && fog.end && *fog.start >= *fog.end) {
        reader.fail(u"the fog's 'start' must lie before its 'end'"_s);
    }
    return fog;
}

} // namespace

namespace QcAtmosphereParser {

QcBiomeAtmosphere parseAtmosphere(const QJsonObject &json, QcJsonFieldReader &reader, const QString &packNamespace)
{
    reader.warnUnknownKeys({u"fog", u"particles", u"ambience", u"music"});
    QcBiomeAtmosphere atmosphere;
    reader.nestedObject(u"fog"_s, [&atmosphere](const QJsonObject &, QcJsonFieldReader &nested) {
        const Fog fog = readFog(nested);
        atmosphere.fogColour = fog.colour;
        atmosphere.fogStart = fog.start;
        atmosphere.fogEnd = fog.end;
    });
    readEntries(json.value(u"particles"_s), reader, QcBiomeAtmosphere::maxParticles, [&atmosphere](QcJsonFieldReader &nested) {
        nested.warnUnknownKeys({u"kind", u"chance"});
        const std::optional<QcParticleKind> kind = kindAt(nested, u"kind"_s);
        const double chance = nested.number(u"chance"_s, 0.0, 0.0, 1.0);
        if (kind) {
            atmosphere.particles.append({*kind, chance});
        }
    });
    reader.nestedObject(u"ambience"_s, [&atmosphere, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) {
        nested.warnUnknownKeys({u"loop", u"additions", u"additionsChance"});
        atmosphere.loop = nested.name(u"loop"_s, packNamespace);
        atmosphere.additions = nested.name(u"additions"_s, packNamespace);
        atmosphere.additionsChance = nested.number(u"additionsChance"_s, 0.0, 0.0, 1.0);
        if (atmosphere.loop.size() > QcBiomeAtmosphere::maxSoundName || atmosphere.additions.size() > QcBiomeAtmosphere::maxSoundName) {
            nested.fail(u"sound names are at most %1 characters"_s.arg(QcBiomeAtmosphere::maxSoundName));
        }
    });
    reader.nestedObject(u"music"_s, [&atmosphere](const QJsonObject &, QcJsonFieldReader &nested) { atmosphere.music = parseMusic(nested); });
    return atmosphere;
}

QList<QcParticleEmitter> parseEmitters(const QJsonValue &value, QcJsonFieldReader &reader)
{
    QList<QcParticleEmitter> emitters;
    readEntries(value, reader, QcParticleEmitter::maxPerBlock, [&emitters](QcJsonFieldReader &nested) {
        nested.warnUnknownKeys({u"kind", u"chance", u"from"});
        const std::optional<QcParticleKind> kind = kindAt(nested, u"kind"_s);
        const double chance = nested.number(u"chance"_s, 0.0, 0.0, 1.0);
        const QString fromName = nested.string(u"from"_s, u"bottom"_s);
        const std::optional<QcParticleEmitter::From> from = QcAtmosphere::emitterFrom(fromName);
        if (!from) {
            nested.fail(u"unknown 'from' '%1' (top, bottom, faces or through)"_s.arg(fromName));
        }
        if (kind && from) {
            emitters.append({*kind, chance, *from});
        }
    });
    return emitters;
}

} // namespace QcAtmosphereParser
