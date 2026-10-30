// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>

/*! \brief Mixer group a sound plays on; each has its own volume setting. */
enum class QcSoundBus : quint8 {
    Effects, // Sound effects ("effects", the default).
    Ambient, // Ambience ("ambient").
    Music, // Music ("music").
};

/*! The number of QcSoundBus values. */
inline constexpr int qcSoundBusCount = 3;

/*! \brief Signal source of a synthesized layer. */
enum class QcSoundWave : quint8 {
    Sine, // A sine tone.
    Square, // A square wave.
    Triangle, // A triangle wave.
    Saw, // A sawtooth wave.
    Noise, // White noise; \c {frequency} is not used.
    /*! Sparse random clicks (gravel, fire); \c {density} impulses per second. */
    Crackle,
};

/*! \brief One synthesized voice of a sound: a source with a pitch sweep, an envelope and simple
    filters.

    Times are in seconds, frequencies in Hz. */
struct QcSoundLayer
{
    /*! The signal source (JSON \c {"wave"}, "noise" by default). */
    QcSoundWave wave = QcSoundWave::Noise;
    /*! The pitch of the tone waves at the start of the layer. */
    double frequency = 440.0;
    /*! Frequency at the end of the layer (exponential sweep); 0 keeps \c {frequency}. */
    double endFrequency = 0.0;
    /*! When the layer begins after the start of the sound. */
    double start = 0.0;
    /*! How long the layer lasts. */
    double duration = 0.2;
    /*! The linear fade-in at the start of the layer, at most \c {duration}. */
    double attack = 0.005;
    /*! Decay after the attack: amplitude (1 - t)^curve over the rest of the layer; 0 holds the
        level. */
    double curve = 2.0;
    /*! The layer's share of the mix, 0..1. */
    double gain = 1.0;
    /*! Cut-off of a low-pass of two one-pole stages (12 dB per octave); 0 switches it off. */
    double lowpass = 0.0;
    /*! Cut-off of a one-pole high-pass; 0 switches it off. */
    double highpass = 0.0;
    /*! How often per second the tremolo dips the level. */
    double tremoloRate = 0.0;
    /*! How deep the tremolo dips the level, 0..1; 0 switches it off. */
    double tremoloDepth = 0.0;
    /*! Impulses per second of the Crackle wave. */
    double density = 200.0;
    /*! Resonant band-pass around this centre frequency (a two-pole state-variable filter, unity
        gain at the centre); 0 switches it off. Noise through it rings like a struck body (a
        plank's knock, a stone's click) without a pitched tone (decision E-879). */
    double bandpass = 0.0;
    /*! Sharpness of the band-pass (its Q): higher rings narrower and longer. */
    double resonance = 1.0;

    friend bool operator==(const QcSoundLayer &, const QcSoundLayer &) = default;
};

/*! \brief A sound effect, ambience loop or jingle described by synthesis parameters (content
    \c {sounds/}). */
struct QcSoundDefinition
{
    /*! The most layers a sound may have. */
    static constexpr int maxLayers = 8;
    /*! The most variants a sound may have. */
    static constexpr int maxVariants = 8;
    /*! The latest start of a layer, in seconds. */
    static constexpr double maxLayerStart = 10.0;
    /*! The longest duration of a layer, in seconds. */
    static constexpr double maxLayerSeconds = 10.0;
    /*! The lowest frequency, in Hz, of a tone, a sweep end and a filter that is switched on. */
    static constexpr double minFrequency = 20.0;
    /*! The highest frequency, in Hz, of a tone, a sweep end and a filter. */
    static constexpr double maxFrequency = 20000.0;
    /*! The largest \c {range}, in metres. */
    static constexpr double maxRange = 128.0;
    /*! The \c {range} of a sound that does not give one, in metres. */
    static constexpr double defaultRange = 16.0;
    /*! The lowest band-pass resonance. */
    static constexpr double minResonance = 0.5;
    /*! The highest band-pass resonance. */
    static constexpr double maxResonance = 30.0;
    /*! The strongest \c {drive}. */
    static constexpr double maxDrive = 8.0;

    /*! The qualified name (JSON \c {"name"}). */
    QString name;
    /*! The mixer group it plays on (JSON \c {"bus"}). */
    QcSoundBus bus = QcSoundBus::Effects;
    /*! Loudness in 0..1, multiplied with the event's volume. */
    double volume = 1.0;
    /*! Random pitch change per play, as a share (0.1 = up to +-10 %). */
    double pitchVariation = 0.0;
    /*! Random loudness change per play, as a share (0.2 = up to 20 % quieter; decision E-879). */
    double volumeVariation = 0.0;
    /*! Soft saturation of the rendering (0 off, up to maxDrive): the quieter parts come up
        towards the peak, so a blast sounds dense and loud while its peak stays where it was
        (decision E-879). */
    double drive = 0.0;
    /*! Differently seeded renderings; one is picked per play. */
    int variants = 1;
    /*! Loops play until stopped (ambience) and are rendered seamless. */
    bool loop = false;
    /*! Distance in metres at which a positioned sound of volume 1 fades out completely. */
    double range = defaultRange;
    /*! The voices mixed into the sound, 1 to maxLayers. */
    QList<QcSoundLayer> layers;

    /*! Returns the length in seconds: the end of the latest layer. */
    double seconds() const;
    /*! Returns an empty string when every value lies within the ranges content may use;
        otherwise the first problem. Wire data is untrusted, so the content sync checks it as well
        as the parser. */
    QString problem() const;

    friend bool operator==(const QcSoundDefinition &, const QcSoundDefinition &) = default;
};
