#pragma once

#include <QString>

#include <optional>

class QSettings;

// How the world is drawn (decision E-899): the classic look or the realistic render mode (stage 45)
// with its quality and exposure. Stored in settings.ini as render/<key>; a value that makes no
// sense gives the default.
struct QcRenderOptions
{
    // Classic: the picture of the game as it always was, straight into the window. Realistic: the
    // world into an HDR picture first, with height fog, bloom and tonemapping on the way to the window.
    enum class Mode { Classic, Realistic };
    // What the realistic mode spends on effects: Low without bloom and ambient occlusion and with
    // the fewest moving lights, Medium with a bloom of mediumBloomLevels, ambient occlusion of
    // mediumOcclusionSamples and mediumMovingLights, High with the wider bloom, more samples and
    // lights; later stages add their effects by quality.
    enum class Quality { Low, Medium, High };

    static constexpr double defaultExposure = 1.0;
    static constexpr double minExposure = 0.5;
    static constexpr double maxExposure = 2.0;
    static constexpr int mediumBloomLevels = 5;
    static constexpr int highBloomLevels = 7;
    // Samples per pixel of the screen-space ambient occlusion (decision E-902).
    static constexpr int mediumOcclusionSamples = 8;
    static constexpr int highOcclusionSamples = 16;
    // Moving lights (held torches, fireflies) the shaders take per frame (decision E-902); the
    // frame uniforms hold room for maxMovingLights.
    static constexpr int lowMovingLights = 4;
    static constexpr int mediumMovingLights = 8;
    static constexpr int maxMovingLights = 16;
    // The sun's shadow map (decision E-903): cascades side by side in one depth texture of
    // `resolution` squared tiles, reaching `distance` blocks from the camera, filtered with
    // `filterTaps` hardware-compared lookups (1, 4 or 9) per pixel.
    struct Shadows
    {
        int cascades = 0;
        int resolution = 0;
        float distance = 0.0f;
        int filterTaps = 0;
        friend bool operator==(const Shadows &, const Shadows &) = default;
    };
    static constexpr int maxShadowCascades = 3;
    // The water of the realistic mode (decision E-906): wave octaves of the surface's normals, how
    // far the refraction bends the picture behind it (0: straight through), the steps of the
    // screen-space reflections (0: sky and sun only) and the strength of the caustics on the
    // ground under shallow water (0: none). All zero in the classic mode, whose water is drawn as
    // it always was.
    struct Water
    {
        int waveOctaves = 0;
        float refraction = 0.0f;
        int reflectionSteps = 0;
        float caustics = 0.0f;
        bool drawn() const { return waveOctaves > 0; }
        friend bool operator==(const Water &, const Water &) = default;
    };

    Mode mode = Mode::Classic;
    Quality quality = Quality::Medium;
    // Brightness of the realistic mode before tonemapping (1: the classic brightness of the mid tones).
    double exposure = defaultExposure;

    bool realistic() const { return mode == Mode::Realistic; }
    // The halvings of the bloom chain, 0 for none (the classic mode and quality Low).
    int bloomLevels() const;
    // Samples per pixel of the ambient occlusion, 0 for none (the classic mode and quality Low).
    int occlusionSamples() const;
    // The most moving lights drawn, 0 in the classic mode (decision E-902).
    int movingLights() const;
    // The sun's shadows of the quality (Low one small cascade, Medium two, High three and the widest
    // filter); no cascades in the classic mode.
    Shadows shadows() const;
    // The water of the quality (Low waves, Fresnel, sky and sun; Medium also refraction and
    // caustics; High also screen-space reflections); nothing in the classic mode.
    Water water() const;

    // "classic", "realistic"; "low", "medium", "high".
    static QString modeName(Mode mode);
    [[nodiscard]] static std::optional<Mode> modeFromName(const QString &name);
    static QString qualityName(Quality quality);
    [[nodiscard]] static std::optional<Quality> qualityFromName(const QString &name);
    // Whether `exposure` is a finite value in [minExposure, maxExposure].
    static bool isExposure(double exposure);

    static QcRenderOptions load(const QSettings &file);
    void save(QSettings &file) const;

    friend bool operator==(const QcRenderOptions &, const QcRenderOptions &) = default;
};
