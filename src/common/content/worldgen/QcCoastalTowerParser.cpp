// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/worldgen/QcCoastalTowerParser.h"

#include "common/content/registry/QcContentNames.h"
#include "common/content/registry/QcJsonFieldReader.h"

#include <QJsonArray>

using namespace Qt::StringLiterals;

namespace QcCoastalTowerParser {

namespace {

// The structures a tower is chosen from, with their weights.
[[nodiscard]] QList<QcCoastalTowerSettings::Structure> parseStructures(const QJsonObject &json, QcJsonFieldReader &reader,
                                                                       const QString &packNamespace)
{
    QList<QcCoastalTowerSettings::Structure> result;
    const QJsonArray structures = json.value(u"structures"_s).toArray();
    for (const auto &entry : structures) {
        const QJsonObject structure = entry.toObject();
        const QString name = structure.value(u"structure"_s).toString();
        const int weight = structure.value(u"weight"_s).toInt(1);
        if (name.isEmpty() || weight < 1) {
            reader.fail(u"'structures' entries need a 'structure' and a positive 'weight'"_s);
            return result;
        }
        result.append({QcContentNames::qualify(name, packNamespace), weight});
    }
    return result;
}

} // namespace

QcCoastalTowerSettings parseCoastalTowers(const QJsonObject &json, QcJsonFieldReader &reader, const QString &packNamespace)
{
    using Settings = QcCoastalTowerSettings;
    Settings towers;
    towers.enabled = reader.boolean(u"enabled"_s, true);
    towers.regionSize = reader.integer(u"regionSize"_s, towers.regionSize, 48, Settings::maxRegionSize);
    towers.chance = reader.number(u"chance"_s, towers.chance, 0.0, 1.0);
    towers.minDistance = reader.integer(u"minDistance"_s, towers.minDistance, 0, towers.regionSize);
    towers.attempts = reader.integer(u"attempts"_s, towers.attempts, 1, 64);
    towers.gridStep = reader.integer(u"gridStep"_s, towers.gridStep, 2, 32);
    towers.maxRise = reader.integer(u"maxRise"_s, towers.maxRise, 0, 32);
    towers.maxSlope = reader.integer(u"maxSlope"_s, towers.maxSlope, 0, 16);
    towers.coastDistance = reader.integer(u"coastDistance"_s, towers.coastDistance, 1, Settings::maxDistance);
    towers.oceanDepth = reader.integer(u"oceanDepth"_s, towers.oceanDepth, 1, 32);
    towers.clearance = reader.integer(u"clearance"_s, towers.clearance, 0, Settings::maxDistance);
    towers.foundation = reader.name(u"foundation"_s, packNamespace);
    towers.maxFoundation = reader.integer(u"maxFoundation"_s, towers.maxFoundation, 0, 64);
    towers.structures = parseStructures(json, reader, packNamespace);
    if (reader.ok() && (towers.foundation.isEmpty() || towers.structures.isEmpty())) {
        reader.fail(u"needs a 'foundation' and 'structures'"_s);
    }
    return towers;
}

} // namespace QcCoastalTowerParser
