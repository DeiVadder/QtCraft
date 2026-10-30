// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcJsonFieldReader.h"
#include "common/content/worldgen/QcStructureDefinition.h"

#include <QJsonObject>

#include <optional>

/*! \brief Parses where the world generator places a structure by itself and what guards it
    (decisions E-590, E-591): \c {groundLayer}, \c {surface} (biomes, region spacing, chance,
    attempts, slope, margin to villages, foundation), \c {guardians} (zones whose creatures come
    back) and \c {spawnArea} (the monsters of its footprint, decision E-871).

    Used by QcStructureParser after the layers, so the zones are checked against the structure's
    size. */
namespace QcStructureSiteParser {

/*! \brief Where and how a structure is placed by the generator: the fields of
    QcStructureDefinition of the same names. \qcfields */
struct Site
{
    int groundLayer = 0;
    std::optional<QcStructureDefinition::Surface> surface;
    QList<QcStructureDefinition::Guardian> guardians;
    std::optional<QcStructureDefinition::SpawnArea> spawnArea;
};

/*! Returns the site read from the structure's \a json through \a reader, which fails on an
    invalid entry. \a structure is the structure read so far (its size bounds the zones and the
    ground layer); names are qualified with \a packNamespace. */
[[nodiscard]] Site parseSite(const QJsonObject &json, const QcStructureDefinition &structure, QcJsonFieldReader &reader,
                             const QString &packNamespace);

} // namespace QcStructureSiteParser
