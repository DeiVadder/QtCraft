// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QJsonObject>

#include <optional>

/*! \brief The surface of one procedural texture for its material maps (decision E-908), before it
    is painted per texel by QcMaterialSynthesizer.

    Values follow the labPBR conventions of the reference game's shader packs: perceptual
    smoothness 0..1, reflectance at normal incidence (F0) as a code 0..229 for dielectrics (F0 =
    code / 255) or a metal code 230..237 (QcMaterialParams::Metal), 255 for a metal coloured by
    the texture itself, porosity 0..1 and emission 0..1. */
struct QcMaterialParams
{
    /*! \brief The labPBR codes of the hardcoded metals (green channel of the specular map). */
    enum class Metal : int {
        None = 0,        // A dielectric, F0 from reflectance
        Iron = 230,      // Iron
        Gold = 231,      // Gold
        Aluminium = 232, // Aluminium
        Chrome = 233,    // Chrome
        Copper = 234,    // Copper
        Lead = 235,      // Lead
        Platinum = 236,  // Platinum
        Silver = 237,    // Silver
        Albedo = 255     // A metal coloured by the texture itself
    };

    /*! \brief What the texels of a second colour get (the ore in a stone, decision E-908): the
        texels nearer to the definition's secondary than to its primary colour. */
    struct Inclusion
    {
        /*! Smoothness of the inclusion's texels. */
        float smoothness = 0.5f;
        /*! F0 * 255 of the inclusion's texels when not metal. */
        int reflectance = 10;
        /*! Metal of the inclusion's texels. */
        Metal metal = Metal::None;
        /*! Emission of the inclusion's texels. */
        float emission = 0.0f;
        /*! Raised by this share of the full height range over the stone around it. */
        float raise = 0.25f;
        friend bool operator==(const Inclusion &, const Inclusion &) = default;
    };

    /*! Perceptual smoothness (0..1). */
    float smoothness = 0.15f;
    /*! F0 * 255 of a dielectric (10: 0.04, most stone and wood); unused for a metal. */
    int reflectance = 10;
    /*! Metal code; None for a dielectric. */
    Metal metal = Metal::None;
    /*! Strength of the normals from the texture's brightness (0: flat). */
    float bump = 1.0f;
    /*! How much water the surface takes in (darker and smoother when wet). */
    float porosity = 0.3f;
    /*! Light of its own: 0 none; else the share of the brightest texels that glow
        (emissionTexels) with this strength. */
    float emission = 0.0f;
    /*! Share (0..1) of the brightest texels that glow when emission > 0. */
    float emissionTexels = 0.35f;
    /*! What the texels of the secondary colour get; unset: the same as the rest. */
    std::optional<Inclusion> inclusion;

    /*! Returns the flat material of textures without maps: smooth 0, F0 0.04, no relief, no
        light. */
    static QcMaterialParams flat();
    /*! Returns \a base with the fields of a "material" object of a texture definition or pack
        spec, \a material (docs/Texturpakete.md): smoothness, reflectance, metal, bump, porosity,
        emission, emissionTexels; unknown or out-of-range values keep the base value. */
    static QcMaterialParams withOverrides(QcMaterialParams base, const QJsonObject &material);
    /*! Returns the metal called \a name ("iron", "gold", …; "albedo"), unset for unknown names. */
    static std::optional<Metal> metalFromName(const QString &name);

    friend bool operator==(const QcMaterialParams &, const QcMaterialParams &) = default;
};
