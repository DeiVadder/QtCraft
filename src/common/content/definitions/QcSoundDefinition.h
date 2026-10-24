#pragma once

#include <QList>
#include <QString>

// Mixer group a sound plays on; each has its own volume setting.
enum class QcSoundBus : quint8 {
    Effects,
    Ambient,
    Music,
};

inline constexpr int qcSoundBusCount = 3;

// Signal source of a synthesized layer.
enum class QcSoundWave : quint8 {
    Sine,
    Square,
    Triangle,
    Saw,
    Noise,
    // Sparse random clicks (gravel, fire); `density` impulses per second.
    Crackle,
};

// One synthesized voice of a sound: a source with a pitch sweep, an envelope and simple filters.
// Times are in seconds, frequencies in Hz.
struct QcSoundLayer
{
    QcSoundWave wave = QcSoundWave::Noise;
    double frequency = 440.0;
    // Frequency at the end of the layer (exponential sweep); 0 keeps `frequency`.
    double endFrequency = 0.0;
    double start = 0.0;
    double duration = 0.2;
    double attack = 0.005;
    // Decay after the attack: amplitude (1 - t)^curve over the rest of the layer; 0 holds the level.
    double curve = 2.0;
    double gain = 1.0;
    // One-pole filters; 0 switches them off.
    double lowpass = 0.0;
    double highpass = 0.0;
    double tremoloRate = 0.0;
    double tremoloDepth = 0.0;
    double density = 200.0;
    // Resonant band-pass around this centre frequency (a two-pole state-variable filter, unity gain at
    // the centre); 0 switches it off. Noise through it rings like a struck body (a plank's knock, a
    // stone's click) without a pitched tone (decision E-879).
    double bandpass = 0.0;
    // Sharpness of the band-pass (its Q): higher rings narrower and longer.
    double resonance = 1.0;

    friend bool operator==(const QcSoundLayer &, const QcSoundLayer &) = default;
};

// A sound effect, ambience loop or jingle described by synthesis parameters (content `sounds/`).
struct QcSoundDefinition
{
    static constexpr int maxLayers = 8;
    static constexpr int maxVariants = 8;
    static constexpr double maxLayerStart = 10.0;
    static constexpr double maxLayerSeconds = 10.0;
    static constexpr double minFrequency = 20.0;
    static constexpr double maxFrequency = 20000.0;
    static constexpr double maxRange = 128.0;
    static constexpr double defaultRange = 16.0;
    static constexpr double minResonance = 0.5;
    static constexpr double maxResonance = 30.0;
    static constexpr double maxDrive = 8.0;

    QString name;
    QcSoundBus bus = QcSoundBus::Effects;
    // Loudness in 0..1, multiplied with the event's volume.
    double volume = 1.0;
    // Random pitch change per play, as a share (0.1 = up to +-10 %).
    double pitchVariation = 0.0;
    // Random loudness change per play, as a share (0.2 = up to 20 % quieter; decision E-879).
    double volumeVariation = 0.0;
    // Soft saturation of the rendering (0 off, up to maxDrive): the quieter parts come up towards the
    // peak, so a blast sounds dense and loud while its peak stays where it was (decision E-879).
    double drive = 0.0;
    // Differently seeded renderings; one is picked per play.
    int variants = 1;
    // Loops play until stopped (ambience) and are rendered seamless.
    bool loop = false;
    // Distance in metres at which a positioned sound of volume 1 fades out completely.
    double range = defaultRange;
    QList<QcSoundLayer> layers;

    // Length in seconds: the end of the latest layer.
    double seconds() const;
    // Empty when every value lies within the ranges content may use; otherwise the first problem.
    // Wire data is untrusted, so the content sync checks it as well as the parser.
    QString problem() const;

    friend bool operator==(const QcSoundDefinition &, const QcSoundDefinition &) = default;
};
