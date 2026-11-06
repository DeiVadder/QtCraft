// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/materials/QcMaterialRules.h"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

using namespace Qt::StringLiterals;
using Metal = QcMaterialParams::Metal;

namespace {

// A surface by pattern: smoothness, F0 code, bump, porosity.
struct Surface
{
    const char *pattern;
    float smoothness;
    int reflectance;
    float bump;
    float porosity;
};

// Rough natural stone and soil, wood, bricks, polished and glassy surfaces (labPBR values of the
// reference game's packs: stone 0.1–0.2 smooth, planks 0.25, glazed tiles 0.8, glass and ice 0.9+).
constexpr std::array<Surface, 21> surfaces{{
    {"speckle", 0.12f, 10, 1.2f, 0.35f},
    {"noise", 0.12f, 10, 1.1f, 0.4f},
    {"lump", 0.14f, 10, 1.3f, 0.3f},
    {"patches", 0.12f, 10, 1.0f, 0.4f},
    {"planks", 0.28f, 10, 0.9f, 0.6f},
    {"log_side", 0.18f, 10, 1.4f, 0.6f},
    {"log_top", 0.22f, 10, 1.0f, 0.6f},
    {"bricks", 0.2f, 10, 1.5f, 0.45f},
    {"chiseled_bricks", 0.22f, 10, 1.4f, 0.4f},
    {"cracked_bricks", 0.16f, 10, 1.6f, 0.45f},
    {"mossy_bricks", 0.14f, 10, 1.4f, 0.55f},
    {"smooth_stone", 0.5f, 10, 0.4f, 0.1f},
    {"glazed_terracotta", 0.82f, 11, 0.35f, 0.0f},
    {"glass", 0.96f, 10, 0.15f, 0.0f},
    {"stained_glass", 0.95f, 10, 0.15f, 0.0f},
    {"firefly_glass", 0.92f, 10, 0.2f, 0.0f},
    {"ice", 0.9f, 5, 0.3f, 0.0f},
    {"packed_ice", 0.78f, 5, 0.4f, 0.0f},
    {"leaves", 0.32f, 10, 0.6f, 0.0f},
    {"lava", 0.25f, 10, 0.8f, 0.0f},
    {"farmland", 0.1f, 10, 1.2f, 0.8f},
}};

// The relief patterns (decision E-1043) say their finish themselves; words of the texture's name
// do not change it.
constexpr std::array<Surface, 32> reliefSurfaces{{
    {"fused_rock", 0.14f, 10, 1.4f, 0.45f},
    {"quartz_ore", 0.14f, 10, 1.4f, 0.45f},
    {"gold_vein_ore", 0.14f, 10, 1.4f, 0.45f},
    {"hollow_sand", 0.08f, 10, 1.2f, 0.7f},
    {"crusted_soil", 0.1f, 10, 1.2f, 0.7f},
    {"column_side", 0.18f, 10, 0.9f, 0.3f},
    {"column_top", 0.2f, 10, 0.9f, 0.3f},
    {"polished_columns", 0.5f, 10, 0.45f, 0.1f},
    {"flow_bands", 0.45f, 10, 0.4f, 0.1f},
    {"glassy_stone", 0.7f, 10, 0.8f, 0.05f},
    {"gilded_stone", 0.7f, 10, 0.8f, 0.05f},
    {"polished_glass", 0.84f, 10, 0.25f, 0.0f},
    {"ashlar", 0.3f, 10, 1.4f, 0.2f},
    {"cracked_ashlar", 0.24f, 10, 1.6f, 0.25f},
    {"carved_ashlar", 0.3f, 10, 1.4f, 0.2f},
    {"fired_bricks", 0.22f, 10, 1.5f, 0.45f},
    {"cracked_fired_bricks", 0.18f, 10, 1.6f, 0.45f},
    {"carved_fired_bricks", 0.22f, 10, 1.4f, 0.4f},
    {"magma_crust", 0.25f, 10, 1.0f, 0.0f},
    {"glow_cluster", 0.5f, 10, 0.7f, 0.0f},
    {"fungal_felt", 0.1f, 10, 1.3f, 0.65f},
    {"moss_cushions", 0.08f, 10, 1.3f, 0.7f},
    {"mat_edge", 0.1f, 10, 1.2f, 0.6f},
    {"rope_stem", 0.2f, 10, 1.3f, 0.4f},
    {"pored_stem", 0.24f, 10, 1.2f, 0.4f},
    {"stem_section", 0.22f, 10, 1.0f, 0.4f},
    {"peeled_fibre", 0.3f, 10, 0.9f, 0.3f},
    {"fibre_boards", 0.3f, 10, 1.0f, 0.3f},
    {"pitted_boards", 0.3f, 10, 1.0f, 0.3f},
    {"wart_lumps", 0.36f, 10, 1.4f, 0.55f},
    {"wart_folds", 0.36f, 10, 1.4f, 0.55f},
    {"glow_lobes", 0.5f, 10, 0.6f, 0.0f},
}};

// The metal a name speaks of, Iron for metal parts without one (bars, rails, lanterns).
Metal metalOfName(const QString &name)
{
    if (name.contains("gold"_L1)) {
        return Metal::Gold;
    }
    if (name.contains("copper"_L1)) {
        return Metal::Copper;
    }
    return Metal::Iron;
}

// The ore in a stone (pattern "ore"), by the ore's name.
QcMaterialParams::Inclusion inclusionOf(const QString &name)
{
    QcMaterialParams::Inclusion ore;
    if (name.contains("iron"_L1) || name.contains("gold"_L1) || name.contains("copper"_L1)) {
        ore.metal = metalOfName(name);
        ore.smoothness = 0.62f;
    } else if (name.contains("diamond"_L1)) {
        ore.reflectance = 43;
        ore.smoothness = 0.92f;
    } else if (name.contains("emerald"_L1)) {
        ore.reflectance = 15;
        ore.smoothness = 0.88f;
    } else if (name.contains("signal"_L1)) {
        ore.smoothness = 0.5f;
        ore.emission = 0.35f;
    } else {
        ore.smoothness = name.contains("coal"_L1) ? 0.45f : 0.6f;
    }
    return ore;
}

// The inclusion of a pattern that says what is in the stone; unset for the others.
std::optional<QcMaterialParams::Inclusion> inclusionOfPattern(const QString &pattern)
{
    QcMaterialParams::Inclusion inclusion;
    if (pattern == "gold_vein_ore"_L1 || pattern == "gilded_stone"_L1) {
        inclusion.metal = Metal::Gold;
        inclusion.smoothness = 0.62f;
        return inclusion;
    }
    if (pattern == "quartz_ore"_L1) {
        inclusion.reflectance = 14;
        inclusion.smoothness = 0.82f;
        return inclusion;
    }
    return std::nullopt;
}

bool isRelief(const QString &pattern)
{
    return std::ranges::any_of(reliefSurfaces, [&pattern](const Surface &surface) { return pattern == QLatin1StringView(surface.pattern); });
}

QcMaterialParams surfaceOf(const QString &pattern)
{
    QcMaterialParams params;
    const auto apply = [&params, &pattern](const Surface &surface) {
        if (pattern == QLatin1StringView(surface.pattern)) {
            params.smoothness = surface.smoothness;
            params.reflectance = surface.reflectance;
            params.bump = surface.bump;
            params.porosity = surface.porosity;
        }
    };
    std::ranges::for_each(surfaces, apply);
    std::ranges::for_each(reliefSurfaces, apply);
    return params;
}

// Metal blocks, raw metal, gems and the metal parts of blocks.
QcMaterialParams withMetal(QcMaterialParams params, const QString &pattern, const QString &name)
{
    const bool metalPart = pattern == "bars"_L1 || pattern == "rail"_L1 || pattern == "chain"_L1 || pattern == "chain_across"_L1
                           || pattern == "lantern"_L1;
    if (pattern == "metal_block"_L1 && (name.contains("diamond"_L1) || name.contains("emerald"_L1))) {
        params.reflectance = name.contains("diamond"_L1) ? 43 : 15;
        params.smoothness = 0.88f;
        params.bump = 0.4f;
        params.porosity = 0.0f;
    } else if (pattern == "metal_block"_L1 || metalPart) {
        params.metal = metalOfName(name);
        params.smoothness = metalPart ? 0.55f : 0.74f;
        params.bump = 0.5f;
        params.porosity = 0.0f;
    } else if (pattern == "raw_block"_L1) {
        params.metal = metalOfName(name);
        params.smoothness = 0.38f;
        params.bump = 1.2f;
        params.porosity = 0.1f;
    }
    return params;
}

} // namespace

QcMaterialParams QcMaterialRules::forTexture(const QString &name, const QJsonObject &definition, bool emitsLight)
{
    const QString pattern = definition.value("pattern"_L1).toString();
    const QString path = name.section(u':', -1);
    QcMaterialParams params = withMetal(surfaceOf(pattern), pattern, path);
    if (pattern == "ore"_L1) {
        params.inclusion = inclusionOf(path);
    } else {
        params.inclusion = inclusionOfPattern(pattern);
    }
    if (!isRelief(pattern) && (path.contains("obsidian"_L1) || path.contains("polished"_L1) || path.startsWith("smooth_"_L1))) {
        params.smoothness = path.contains("obsidian"_L1) ? 0.86f : 0.5f;
        params.bump = 0.45f;
    }
    if (pattern == "lava"_L1) {
        params.emission = 1.0f;
        params.emissionTexels = 1.0f;
    } else if (pattern == "magma_crust"_L1) {
        // Only the molten rock in the cracks glows, not the crust; at full strength the bloom of
        // the fine cracks would cover the plates.
        params.emission = 0.5f;
        params.emissionTexels = 0.15f;
    } else if (pattern == "glow_cluster"_L1 || pattern == "glow_lobes"_L1) {
        // Its colours are nearly white already; at full strength the bloom hides the lumps.
        params.emission = 0.4f;
    } else if (emitsLight || pattern == "fire"_L1) {
        params.emission = 1.0f;
    }
    return QcMaterialParams::withOverrides(params, definition.value("material"_L1).toObject());
}
