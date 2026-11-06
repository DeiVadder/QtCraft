// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcIdList.h"
#include "common/core/QcIndexId.h"
#include "common/core/QcXzRect.h"
#include "worldgen/structures/QcStructurePlacement.h"
#include "worldgen/villages/QcPieceVariation.h"

#include <QList>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>

class QcTerrainGenerator;

/*! \brief Plans where the structures with a \c {surface} rule stand (decision E-590), one region of
    one kind at a time, from the seed, the structure's name and the terrain noise alone (columns,
    never generated chunks), like the villages: whether the region has a copy (\c {chance}), then
    the first of \c {attempts} places (a centre and a turn per attempt) whose footprint and blended
    ground (decision E-889) lie wholly inside the region, on land above the sea in one of the rule's
    biomes at its middle and along its edges, with its ground varying by at most \c {maxSlope} and
    its floor on the lowest ground, the ground right beside its walls levelled by the blend, and
    clear of what \c {occupied} reports (villages, other kinds).

    With \c {waterDepth} the samples may lie under that much water; with \c {preferWater} the
    first place whose middle lies under water wins over the first that fits (a witch hut on
    stilts, decision E-622). A kind whose structure has \c {choices} or \c {parts} builds each
    copy from a selection drawn from the region's own positions (decision E-630). Thread-safe. */
class QcSurfacePlanner
{
public:
    /*! Blocks between two samples of the ground along a footprint's edge. */
    static constexpr int edgeStep = 4;

    /*! \brief Tells the kinds of a planner apart from the ids of other lists (QcIndexId). */
    struct KindTag;
    /*! The index of a kind in kinds() (decision E-952). */
    using KindId = QcIndexId<KindTag>;

    /*! \brief A kind of surface structure: a structure with a \c {surface} rule, resolved. */
    struct Kind
    {
        /*! The structure's content name. */
        QString name;
        /*! The structure and the ways its copies vary (its \c {choices} and \c {parts}). */
        QcPieceVariation variation;
        /*! The structure's \c {surface} rule. */
        QcStructureDefinition::Surface rule;
        /*! The block that fills the ground below a copy down to solid ground; air for none. */
        QcBlockId foundation = qcAirBlockId;
        /*! The kind's own seed, mixed from the world seed and the name. */
        quint64 seed = 0;

        /*! Returns the structure as written; every copy has its size and extras. */
        const std::shared_ptr<const QcStructure> &structure() const { return variation.base(); }
    };

    /*! Whether something already stands within \c {margin} of a footprint (QcSurfaceStructures
        asks the villages and the kinds planned before this one). */
    using Occupied = std::function<bool(KindId kind, QcXzRect footprint, int margin)>;

    /*! Resolves the structures of \a content with a \c {surface} rule into kinds, each with its
        own seed from \a seed, planned on the columns of \a terrain. Structures with an unknown
        block or foundation are left out with a warning (warnings());
        kinds are sorted by name, so their order does not depend on the order the content was
        loaded in. */
    QcSurfacePlanner(const QcTerrainGenerator &terrain, const QcContentRegistry &content, quint64 seed);

    /*! Returns the warnings from resolving the structures, among them those left out. */
    const QStringList &warnings() const { return m_warnings; }

    /*! Returns the kinds, sorted by name. */
    [[nodiscard]] QcIdList<KindId, Kind> kinds() const { return QcIdList<KindId, Kind>(m_kinds); }
    /*! Returns the index of the kind \a name, if any. */
    [[nodiscard]] std::optional<KindId> kindIndex(const QString &name) const;
    /*! Returns the copy of kind \a kind in region (\a regionX, \a regionZ) of its spacing, if
        any; \a occupied rules out places too close to something already standing. */
    [[nodiscard]] std::optional<QcStructurePlacement> plan(KindId kind, int regionX, int regionZ, const Occupied &occupied) const;
    /*! Returns the footprint of \a placement, a placed copy. */
    static QcXzRect footprintOf(const QcStructurePlacement &placement);

private:
    // The floor (the top block of the lowest ground) of a footprint where every sample (the middle,
    // the edges every edgeStep blocks) is land of the rule's biomes above the sea and the ground
    // varies by at most `maxSlope`.
    [[nodiscard]] std::optional<int> floorFor(const Kind &kind, QcXzRect rect) const;
    // Whether the blend (decision E-889) levels every column right beside the footprint's walls to
    // `floor`: none of them buried, so every opening at the floor leads out.
    [[nodiscard]] bool levelsBeside(QcXzRect rect, int floor, int blend) const;
    // The structure of the copy in the region: the kind's own, or its variation drawn from the region.
    [[nodiscard]] std::shared_ptr<const QcStructure> structureOf(const Kind &kind, int regionX, int regionZ) const;

    const QcTerrainGenerator &m_terrain;
    const QcContentRegistry &m_content;
    QList<Kind> m_kinds;
    QStringList m_warnings;
};
