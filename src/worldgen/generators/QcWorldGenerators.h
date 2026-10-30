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

/*! \brief The world types a world can be created with; world.json names the type and its options
    (decisions E-221, E-223).

    Worlds without a type are terrain worlds. */
namespace QcWorldGenerators {

/*! The name of the terrain world type. */
inline constexpr auto terrain = "terrain";
/*! The name of the flat world type (QcFlatGenerator). */
inline constexpr auto flat = "flat";
/*! The name of the void world type (QcVoidGenerator). */
inline constexpr auto empty = "void";
/*! The name of the showcase world type (QcShowcaseGenerator). */
inline constexpr auto showcase = "showcase";

/*! Returns the names of the world types in the order of the menu. */
QStringList types();
/*! Returns whether \a type names a world type. */
bool isKnown(const QString &type);
/*! The longest options a world can store (world.json). */
inline constexpr qsizetype maxOptionsLength = 4096;

/*! Returns whether \a options suit the world type \a type (an error also for an unknown type),
    with the blocks of \a content: the layer preset of a flat world, the platform of a
    void world (QcVoidGenerator::parseOptions; empty: the defaults); the other types take no
    options. The length limit applies to the text as given. */
[[nodiscard]] QcStatus checkOptions(const QString &type, const QString &options, const QcContentRegistry &content);
/*! Returns the options a new world of \a type stores for \a options: checked, with the current
    defaults of \a content filled in (the flat preset, the void platform), so that later changes
    of those defaults by mods or updates do not change existing worlds. */
[[nodiscard]] QcResult<QString> optionsToStore(const QString &type, const QString &options, const QcContentRegistry &content);
/*! Returns a generator of \a type for \a content, \a seed and \a options that generates as world
    generator version \a version does (decision E-922, QcGeneratorVersion; a world passes its
    own, QcWorldInfo::generatorVersion); nullptr for unknown types and versions. Only the terrain
    generator uses \a seed. The showcase always generates with the current code, whatever
    the version. What the generator could not resolve is in its warnings(). */
std::unique_ptr<QcWorldGenerator> create(const QString &type, std::shared_ptr<const QcContentRegistry> content, qint64 seed, const QString &options,
                                         int version);

} // namespace QcWorldGenerators
