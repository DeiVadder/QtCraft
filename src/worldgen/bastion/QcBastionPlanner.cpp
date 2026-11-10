// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/bastion/QcBastionPlanner.h"

#include "worldgen/bastion/QcBastionForms.h"
#include "worldgen/nether/QcNetherComplexes.h"
#include "worldgen/nether/QcNetherTerrain.h"
#include "worldgen/terrain/QcNoise.h"

QcBastionPlanner::QcBastionPlanner(const QcNetherDefinitions &definitions, const QcNetherTerrain &terrain, const QcNetherComplexes &complexes, quint64 seed)
    : m_definitions(definitions)
    , m_terrain(terrain)
    , m_complexes(complexes)
    , m_seed(qcMixSeed(seed, 0xba57))
{
}

std::shared_ptr<const QcBastionPlan> QcBastionPlanner::plan(int regionX, int regionZ) const
{
    if (!enabled() || m_complexes.kindOf(regionX, regionZ) != QcNetherComplexes::Kind::Bastion) {
        return nullptr;
    }
    QcBastionPlan plan;
    plan.regionX = regionX;
    plan.regionZ = regionZ;
    plan.centre = m_complexes.bastionSite(regionX, regionZ);
    plan.form = static_cast<QcBastionPlan::Form>(static_cast<int>(qcPositionRandom(seed(), regionX, 0, regionZ) * QcBastionPlan::formCount));
    return draw(std::move(plan), static_cast<int>(qcPositionRandom(seed(), regionX, 1, regionZ) * 4.0));
}

std::shared_ptr<const QcBastionPlan> QcBastionPlanner::planAt(QcBastionPlan::Form form, QcBlockPos centre) const
{
    QcBastionPlan plan;
    plan.centre = centre;
    plan.form = form;
    return draw(std::move(plan), 0);
}

std::shared_ptr<QcBastionPlan> QcBastionPlanner::draw(QcBastionPlan plan, int turns) const
{
    auto drawn = std::make_shared<QcBastionPlan>(std::move(plan));
    int rolled = 1;
    const auto roll = [&rolled, this, &drawn]() { return qcPositionRandom(seed(), drawn->regionX + static_cast<int>(drawn->form) * 7919, ++rolled, drawn->regionZ); };
    const auto isRock = [this](QcBlockPos cell) { return m_terrain.isSolid(cell.x, cell.y, cell.z); };
    QcBastionSketch sketch(*drawn, turns, roll, isRock);
    QcBastionForms::draw(sketch, drawn->form);
    return drawn;
}
