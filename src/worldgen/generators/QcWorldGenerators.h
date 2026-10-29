// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcResult.h"
#include "common/world/QcGeneratorVersion.h"
#include "worldgen/generators/QcWorldGenerator.h"

#include <QString>
#include <QStringList>

#include <memory>

// The world types a world can be created with; world.json names the type and its options
// (decisions E-221, E-223). Worlds without a type are terrain worlds.
namespace QcWorldGenerators {

inline constexpr auto terrain = "terrain";
inline constexpr auto flat = "flat";
inline constexpr auto empty = "void";
inline constexpr auto showcase = "showcase";

// In the order of the menu.
QStringList types();
bool isKnown(const QString &type);
// Longest options a world can store (world.json).
inline constexpr qsizetype maxOptionsLength = 4096;

// Whether `options` suit the type: the layer preset of a flat world, the platform of a void world
// (QcVoidGenerator::parseOptions; empty: the defaults); the other types take no options. The length
// limit applies to the text as given.
[[nodiscard]] QcStatus checkOptions(const QString &type, const QString &options, const QcContentRegistry &content);
// The options a new world of `type` stores: checked, with the content's current defaults filled in
// (the flat preset, the void platform), so that later changes of those defaults by mods or updates
// do not change existing worlds.
[[nodiscard]] QcResult<QString> optionsToStore(const QString &type, const QString &options, const QcContentRegistry &content);
// A generator of `type` that generates as world generator version `version` does (decision E-922,
// QcGeneratorVersion; a world passes its own, QcWorldInfo::generatorVersion); nullptr for unknown
// types and versions. The showcase always generates with the current code, whatever the version.
// What the generator could not resolve is in its warnings().
std::unique_ptr<QcWorldGenerator> create(const QString &type, std::shared_ptr<const QcContentRegistry> content, qint64 seed, const QString &options,
                                         int version);

} // namespace QcWorldGenerators
