#pragma once

#include "server/blockentities/QcBlockEntities.h"
#include "server/machines/QcMachineRules.h"

// The rules of machines and the block entities they work on, as the server keeps them (decision
// E-357), for tests of machines without a server.
struct QcTestMachines
{
    explicit QcTestMachines(const std::shared_ptr<const QcContentRegistry> &content)
        : rules(content)
        , entities(content)
    {
    }

    bool add(QcBlockPos pos, QcBlockId block) { return entities.ensure(pos, block); }
    QcBlockEntities::Machine *find(QcBlockPos pos) { return entities.machine(pos); }
    void tick(const QcBlockAccess &world, double dt) { [[maybe_unused]] const QList<QcBlockPos> changed = rules.tick(entities, world, dt); }
    double burnFraction(const QcBlockEntities::Machine &machine) const { return rules.burnFraction(machine); }
    double progressFraction(const QcBlockEntities::Machine &machine, QcBlockId block) const { return rules.progressFraction(machine, block); }
    qsizetype count() const { return entities.machineCount(); }

    QcMachineRules rules;
    QcBlockEntities entities;
};
