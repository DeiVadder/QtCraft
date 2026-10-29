// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/parser/QcSoundParser.h"

#include "common/content/parser/QcJsonFieldReader.h"

#include <QJsonArray>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

[[nodiscard]] std::optional<QcSoundWave> waveFromName(const QString &name)
{
    static const QHash<QString, QcSoundWave> waves = {
        {u"sine"_s, QcSoundWave::Sine},   {u"square"_s, QcSoundWave::Square}, {u"triangle"_s, QcSoundWave::Triangle},
        {u"saw"_s, QcSoundWave::Saw},     {u"noise"_s, QcSoundWave::Noise},   {u"crackle"_s, QcSoundWave::Crackle},
    };
    const auto it = waves.constFind(name);
    return it == waves.constEnd() ? std::nullopt : std::optional<QcSoundWave>(*it);
}

// Reads one entry of 'layers'; problems are left in the reader.
QcSoundLayer parseLayer(QcJsonFieldReader &layerReader)
{
    QcSoundLayer layer;

    const std::optional<QcSoundWave> wave = waveFromName(layerReader.string(u"wave"_s, u"noise"_s));
    if (!wave) {
        layerReader.fail(u"'wave' must be sine, square, triangle, saw, noise or crackle"_s);
    }
    layer.wave = wave.value_or(QcSoundWave::Noise);

    constexpr double low = QcSoundDefinition::minFrequency;
    constexpr double high = QcSoundDefinition::maxFrequency;
    layer.frequency = layerReader.number(u"frequency"_s, layer.frequency, low, high);
    layer.endFrequency = layerReader.number(u"endFrequency"_s, 0.0, 0.0, high);
    layer.start = layerReader.number(u"start"_s, 0.0, 0.0, QcSoundDefinition::maxLayerStart);
    layer.duration = layerReader.number(u"duration"_s, layer.duration, 0.001, QcSoundDefinition::maxLayerSeconds);
    layer.attack = layerReader.number(u"attack"_s, std::min(layer.attack, layer.duration), 0.0, layer.duration);
    layer.curve = layerReader.number(u"curve"_s, layer.curve, 0.0, 8.0);
    layer.gain = layerReader.number(u"gain"_s, 1.0, 0.0, 1.0);
    layer.lowpass = layerReader.number(u"lowpass"_s, 0.0, 0.0, high);
    layer.highpass = layerReader.number(u"highpass"_s, 0.0, 0.0, high);
    layer.bandpass = layerReader.number(u"bandpass"_s, 0.0, 0.0, high);
    layer.resonance = layerReader.number(u"resonance"_s, 1.0, QcSoundDefinition::minResonance, QcSoundDefinition::maxResonance);
    layer.tremoloRate = layerReader.number(u"tremoloRate"_s, 0.0, 0.0, 50.0);
    layer.tremoloDepth = layerReader.number(u"tremoloDepth"_s, 0.0, 0.0, 1.0);
    layer.density = layerReader.number(u"density"_s, layer.density, 1.0, 5000.0);

    return layer;
}

} // namespace

namespace QcSoundParser {

[[nodiscard]] QcResult<QcSoundDefinition> parseSound(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"sound"_s, {}};
    QcSoundDefinition sound;
    sound.name = reader.name(u"name"_s, packNamespace);
    if (sound.name.isEmpty()) {
        return qcError(reader.ok() ? u"sound: 'name' is required"_s : reader.error);
    }

    reader.context = u"sound '%1'"_s.arg(sound.name);
    const QString bus = reader.string(u"bus"_s, u"effects"_s);
    if (bus == u"ambient"_s) {
        sound.bus = QcSoundBus::Ambient;
    } else if (bus == u"music"_s) {
        sound.bus = QcSoundBus::Music;
    } else if (bus != u"effects"_s) {
        reader.fail(u"'bus' must be effects, ambient or music"_s);
    }

    sound.volume = reader.number(u"volume"_s, 1.0, 0.0, 1.0);
    sound.pitchVariation = reader.number(u"pitchVariation"_s, 0.0, 0.0, 0.5);
    sound.volumeVariation = reader.number(u"volumeVariation"_s, 0.0, 0.0, 0.5);
    sound.drive = reader.number(u"drive"_s, 0.0, 0.0, QcSoundDefinition::maxDrive);
    sound.variants = reader.integer(u"variants"_s, 1, 1, QcSoundDefinition::maxVariants);
    sound.loop = reader.boolean(u"loop"_s, false);
    sound.range = reader.number(u"range"_s, QcSoundDefinition::defaultRange, 1.0, QcSoundDefinition::maxRange);

    const QJsonValue layers = json.value(u"layers"_s);
    if (!layers.isArray() || layers.toArray().isEmpty() || layers.toArray().size() > QcSoundDefinition::maxLayers) {
        reader.fail(u"'layers' must be an array of 1 to %1 layers"_s.arg(QcSoundDefinition::maxLayers));
    }

    const QJsonArray layerList = layers.toArray();
    for (const auto &entry : layerList) {
        if (!reader.ok()) {
            break;
        }
        if (!entry.isObject()) {
            reader.fail(u"'layers' entries must be objects"_s);
            break;
        }

        const QJsonObject layerJson = entry.toObject();
        QcJsonFieldReader layerReader{layerJson, reader.context + u" layer"_s, {}};
        const QcSoundLayer layer = parseLayer(layerReader);

        if (!layerReader.ok()) {
            reader.fail(layerReader.error);
        }
        sound.layers.append(layer);
    }

    if (reader.ok()) {
        // Filter and sweep frequencies of 0 mean "off"; other values must be audible.
        if (const QString problem = sound.problem(); !problem.isEmpty()) {
            reader.fail(problem);
        }
    }

    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return sound;
}

} // namespace QcSoundParser
