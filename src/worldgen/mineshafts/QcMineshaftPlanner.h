// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/content/worldgen/QcMineshaftDefinition.h"
#include "worldgen/mineshafts/QcMineshaftAccess.h"
#include "worldgen/mineshafts/QcMineshaftBlocks.h"
#include "worldgen/mineshafts/QcMineshaftDetails.h"
#include "worldgen/mineshafts/QcMineshaftPlan.h"

#include <QtGlobal>

#include <memory>
#include <optional>

struct QcMineshaftFrame;
class QcMineshaftNetwork;
class QcMineshaftSite;
class QcTerrainGenerator;

/*! \brief Plans the mine shaft of a region (decision E-570) from the seed and the terrain noise:
    whether the region has one (\c {chance}), how many levels, a place for the central room deep
    enough with a solid floor, the lattice network grown level by level (QcMineshaftNetwork, judged
    by QcMineshaftSite), laid out as nodes and corridors (QcMineshaftFrame), then rooms from
    structures and the surface entrance (QcMineshaftAccess) and the corridors' details
    (QcMineshaftDetails).

    Thread-safe: plan() only reads immutable state. */
class QcMineshaftPlanner
{
public:
    /*! Plans with the mine shaft settings of \a content over the noise of \a terrain, seeded
        from \a seed. Unresolvable blocks switch mine shafts off with a warning (warnings()). */
    QcMineshaftPlanner(const QcTerrainGenerator &terrain, const QcContentRegistry &content, quint64 seed);
    ~QcMineshaftPlanner();
    Q_DISABLE_COPY_MOVE(QcMineshaftPlanner)

    /*! Returns whether mine shafts are planned: enabled in the settings and every block known. */
    bool enabled() const { return m_enabled; }
    /*! Returns the mine shaft settings of the content. */
    const QcMineshaftSettings &settings() const { return m_settings; }
    /*! Returns the problems found in the settings (unknown blocks, unknown entrance biomes). */
    const QStringList &warnings() const { return m_warnings; }
    /*! Returns the resolved blocks. */
    const QcMineshaftBlocks &blocks() const { return m_blocks; }
    /*! Returns the mine shaft of the region \a regionX, \a regionZ; null without one. */
    [[nodiscard]] std::shared_ptr<const QcMineshaftPlan> plan(int regionX, int regionZ) const;

private:
    [[nodiscard]] std::optional<QcBlockPos> findCentre(QcMineshaftSite &site, const QcMineshaftPlan &plan, int levels) const;
    // Nodes and corridors of the network in the world.
    [[nodiscard]] static QList<QcMineshaftPlan::Node> nodesOf(const QcMineshaftNetwork &network, const QcMineshaftFrame &frame);
    [[nodiscard]] static QList<QcMineshaftPlan::Corridor> corridorsOf(const QcMineshaftNetwork &network, const QcMineshaftFrame &frame);
    // A random value in [0, 1) of the plan's region, a purpose and an index.
    double random(const QcMineshaftPlan &plan, int purpose, int index) const;

    const QcTerrainGenerator &m_terrain;
    QcMineshaftSettings m_settings;
    QStringList m_warnings;
    quint64 m_seed = 0;
    bool m_enabled = false;
    QcMineshaftBlocks m_blocks;
    std::unique_ptr<QcMineshaftAccess> m_access;
    std::unique_ptr<QcMineshaftDetails> m_details;
};
