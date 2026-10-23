#pragma once

#include "common/content/definitions/QcStructureDefinition.h"
#include "common/core/QcBlockPos.h"
#include "worldgen/structures/QcStructurePlacement.h"
#include "worldgen/terrain/QcNoise.h"

#include <QList>

#include <algorithm>

// A zone of a placed structure whose guardians come back by themselves (decision E-591): the
// desert temple's cellar and its sand golem. The world generator names the zones near a place
// (QcWorldGenerator::guardSitesNear) from its plans alone, so they need no saving; the server keeps
// their cooldowns while it runs (QcGuardians).
struct QcGuardSite
{
    // The same for the same zone in every session: from its corner and its creature's name.
    quint64 id = 0;
    // The zone in world cells, both corners inside.
    QcBlockPos min;
    QcBlockPos max;
    // The structure's rule with `from` and `to` as written (local).
    QcStructureDefinition::Guardian rule;

    bool contains(QcBlockPos pos) const
    {
        return pos.x >= min.x && pos.y >= min.y && pos.z >= min.z && pos.x <= max.x && pos.y <= max.y && pos.z <= max.z;
    }

    // The guardian zones of a placed copy of a structure.
    static QList<QcGuardSite> of(const QcStructurePlacement &placement)
    {
        QList<QcGuardSite> sites;
        for (const QcStructureDefinition::Guardian &rule : placement.structure().guardians) {
            const QcBlockPos a = placement.toWorld(rule.from);
            const QcBlockPos b = placement.toWorld(rule.to);
            QcGuardSite site;
            site.min = {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
            site.max = {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
            site.id = qcMixSeed(qcMixSeed(qcMixSeed(qcNameSalt(rule.mob), static_cast<quint32>(site.min.x)),
                static_cast<quint32>(site.min.y)), static_cast<quint32>(site.min.z));
            site.rule = rule;
            sites.append(site);
        }
        return sites;
    }
};
