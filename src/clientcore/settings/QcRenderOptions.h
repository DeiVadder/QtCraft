// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

#include <optional>

class QSettings;

/*! \brief How the world is drawn (decision E-899): the classic look or the realistic render mode
    (stage 45) with its quality and exposure.

    Stored in settings.ini as render/<key>; a value that makes no sense gives the default. */
struct QcRenderOptions
{
    /*! \brief Classic: the picture of the game as it always was, straight into the window.

        Realistic: the world into an HDR picture first, with height fog, bloom and tonemapping on
        the way to the window. */
    enum class Mode {
        Classic,  // the classic look
        Realistic // the realistic render mode
    };
    /*! \brief What the realistic mode spends on effects: Low without bloom and ambient occlusion
        and with the fewest moving lights, Medium with a bloom of mediumBloomLevels, ambient
        occlusion of mediumOcclusionSamples and mediumMovingLights, High with the wider bloom,
        more samples and lights; later stages add their effects by quality. */
    enum class Quality {
        Low,    // no bloom and ambient occlusion, the fewest moving lights
        Medium, // the default
        High    // the most of every effect
    };

    /*! The exposure of a new setting: the classic brightness. */
    static constexpr double defaultExposure = 1.0;
    /*! Darkest exposure that can be set. */
    static constexpr double minExposure = 0.5;
    /*! Brightest exposure that can be set. */
    static constexpr double maxExposure = 2.0;
    /*! Halvings of the bloom chain on quality Medium. */
    static constexpr int mediumBloomLevels = 5;
    /*! Halvings of the bloom chain on quality High. */
    static constexpr int highBloomLevels = 7;
    /*! Samples per pixel of the screen-space ambient occlusion (decision E-902). */
    static constexpr int mediumOcclusionSamples = 8;
    /*! Samples per pixel of the ambient occlusion on quality High. */
    static constexpr int highOcclusionSamples = 16;
    /*! Moving lights (held torches, fireflies) the shaders take per frame (decision E-902); the
        frame uniforms hold room for maxMovingLights. \qcshared */
    static constexpr int lowMovingLights = 4;
    static constexpr int mediumMovingLights = 8;
    static constexpr int maxMovingLights = 16;
    /*! \brief The sun's shadow map (decision E-903): cascades side by side in one depth texture
        of \c {resolution} squared tiles, reaching \c {distance} blocks from the camera, filtered
        with \c {filterTaps} hardware-compared lookups (1, 4 or 9) per pixel. \qcfields */
    struct Shadows
    {
        int cascades = 0;
        int resolution = 0;
        float distance = 0.0f;
        int filterTaps = 0;
        friend bool operator==(const Shadows &, const Shadows &) = default;
    };
    /*! Most cascades of the sun's shadow map (quality High). */
    static constexpr int maxShadowCascades = 3;
    /*! \brief The water of the realistic mode (decision E-906): wave octaves of the surface's
        normals, how far the refraction bends the picture behind it (0: straight through), the steps
        of the screen-space reflections (0: sky and sun only) and the strength of the caustics on
        the ground under shallow water (0: none).

        All zero in the classic mode, whose water is drawn as it always was. \qcfields */
    struct Water
    {
        int waveOctaves = 0;
        float refraction = 0.0f;
        int reflectionSteps = 0;
        float caustics = 0.0f;
        /*! Returns whether the realistic water is drawn (it has waves). */
        bool drawn() const { return waveOctaves > 0; }
        friend bool operator==(const Water &, const Water &) = default;
    };

    /*! \brief The material maps of the realistic mode (decision E-908): relief from the normal
        maps, smoothness, metal and reflectance for the sun's highlight and the sky's reflection,
        light of its own (\c {maps}); with \c {lightHighlights} the moving lights also make
        highlights.

        Nothing in the classic mode and on quality Low, which draw flat surfaces. \qcfields */
    struct Materials
    {
        bool maps = false;
        bool lightHighlights = false;
        friend bool operator==(const Materials &, const Materials &) = default;
    };

    /*! The classic look or the realistic render mode. */
    Mode mode = Mode::Classic;
    /*! What the realistic mode spends on effects. */
    Quality quality = Quality::Medium;
    /*! Brightness of the realistic mode before tonemapping (1: the classic brightness of the mid
        tones). */
    double exposure = defaultExposure;

    /*! Returns whether the realistic render mode is chosen. */
    bool realistic() const { return mode == Mode::Realistic; }
    /*! Returns the halvings of the bloom chain, 0 for none (the classic mode and quality Low). */
    int bloomLevels() const;
    /*! Returns the samples per pixel of the ambient occlusion, 0 for none (the classic mode and
        quality Low). */
    int occlusionSamples() const;
    /*! Returns the most moving lights drawn, 0 in the classic mode (decision E-902). */
    int movingLights() const;
    /*! Returns the sun's shadows of the quality (Low one small cascade, Medium two, High three
        and the widest filter); no cascades in the classic mode. */
    Shadows shadows() const;
    /*! Returns the water of the quality (Low waves, Fresnel, sky and sun; Medium also refraction
        and caustics; High also screen-space reflections); nothing in the classic mode. */
    Water water() const;
    /*! Returns the material maps of the quality (Medium the maps, High also the moving lights'
        highlights); none in the classic mode and on Low. */
    Materials materials() const;

    /*! Returns the name of \a mode in settings.ini: "classic" or "realistic". */
    static QString modeName(Mode mode);
    /*! Returns the mode named \a name (modeName()); nothing for an unknown name. */
    [[nodiscard]] static std::optional<Mode> modeFromName(const QString &name);
    /*! Returns the name of \a quality in settings.ini: "low", "medium" or "high". */
    static QString qualityName(Quality quality);
    /*! Returns the quality named \a name (qualityName()); nothing for an unknown name. */
    [[nodiscard]] static std::optional<Quality> qualityFromName(const QString &name);
    /*! Returns whether \a exposure is a finite value in [minExposure, maxExposure]. */
    static bool isExposure(double exposure);

    /*! Returns the options stored in \a file; defaults for missing values or values that make no
        sense. */
    static QcRenderOptions load(const QSettings &file);
    /*! Stores the options in \a file. */
    void save(QSettings &file) const;

    friend bool operator==(const QcRenderOptions &, const QcRenderOptions &) = default;
};
