// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/content/worldgen/QcWorldgenDefinitions.h"
#include "common/core/QcBlockPos.h"

#include <array>
#include <functional>

class QcChunk;
class QcContentRegistry;

/*! \brief Block layout of tree and column features, shared by world generation and growing
    saplings so that grown trees look like generated ones. */
namespace QcTreeShape {

/*! \brief Crown of a tree (decision E-822): the small round and cone crowns, a bush of leaves
    around a single log, and the giant trees standing on a 2×2 trunk (a tall spruce with a
    layered cone, a jungle giant with branches and a wide flat crown). */
enum class Crown : quint8 {
    Round, // Two wide layers under two narrow ones, ragged corners.
    Cone, // Rings shrinking towards the top, from two blocks above the ground.
    Bush, // Leaves around and over the logs.
    GiantCone, // A tall spruce on a 2×2 trunk with a layered cone.
    GiantRound // A jungle giant on a 2×2 trunk with branches and a wide flat crown.
};

/*! \brief The shape of one feature with its blocks resolved, as paramsOf() reads it from a
    feature definition. */
struct Params
{
    /*! The kind of feature: a tree gets a crown and vines, a column only its trunk, a plant one
        block. */
    QcFeatureDefinition::Type type = QcFeatureDefinition::Type::Tree;
    /*! The trunk block (the block of a plant). */
    QcBlockId trunk = qcAirBlockId;
    /*! The trunk lying along x and z (branches of jungle giants); the trunk itself when it has
        no axis. \qcshared */
    QcBlockId trunkX = qcAirBlockId;
    QcBlockId trunkZ = qcAirBlockId;
    /*! The leaves of the crown. */
    QcBlockId leaves = qcAirBlockId;
    /*! The lowest and highest trunk height, in blocks. \qcshared */
    int minHeight = 4;
    int maxHeight = 6;
    /*! The crown of a tree. */
    Crown crown = Crown::Round;
    /*! Vines hung on the trunk and the lowest leaves, one state per side the vine holds on to
        (north, east, south, west); air when the tree has none. */
    std::array<QcBlockId, 4> vines{};

    /*! Returns whether the crown is a giant one. Giant trees stand on four trunk blocks:
        \c {base} and the blocks east, south and south-east of it. */
    bool giant() const { return crown == Crown::GiantCone || crown == Crown::GiantRound; }
};

/*! Receives every block of the feature; \c {onlyIntoAir} marks blocks (leaves, vines) that may
    replace only what mayReplace() allows. Returns whether the block was placed (or, outside the
    area the receiver writes, would be): vines hang only on blocks that were. */
using Put = std::function<bool(QcBlockPos pos, QcBlockId block, bool onlyIntoAir)>;

/*! Returns whether \a block of a tree with \a params may go where \a existing is: anything
    when \a onlyIntoAir is \c false (trunks); otherwise air, and for a block that is none of the
    tree's vine states also one of them (leaves over the vines of another tree; a vine of this one
    may then hold on to them). */
[[nodiscard]] bool mayReplace(const Params &params, QcBlockId existing, QcBlockId block, bool onlyIntoAir);

/*! Returns the crown named by a feature's \a shape ("round", "cone", "bush", "giant_cone",
    "giant_round"). */
std::optional<Crown> crownOf(const QString &shape);
/*! Returns the shape of \a feature with the blocks of \a content (air for unknown blocks). */
Params paramsOf(const QcContentRegistry &content, const QcFeatureDefinition &feature);
/*! Returns the vine states of the block \a vine with the blocks of \a content (air for air), in
    the order of Params::vines. */
std::array<QcBlockId, 4> vinesOf(const QcContentRegistry &content, QcBlockId vine);
/*! Returns \a trunk lying along x and along z (its \c axis property set with \a content), as
    Params::trunkX and trunkZ. */
std::pair<QcBlockId, QcBlockId> lyingTrunks(const QcContentRegistry &content, QcBlockId trunk);

/*! Returns how many blocks the feature with \a params reaches sideways from \c {base} (at most,
    for every height); 0 for features that are no tree. */
int reach(const Params &params);
/*! Returns the trunk height of a feature with \a params for a \a random value in [0, 1). */
int height(const Params &params, double random);
/*! Emits the feature with \a params standing on \a base (the lowest trunk block; of a giant the
    north-west one) to \a put. \a random picks the trunk height; \a seed decides the ragged
    corners of crowns, the branches and the vines. */
void build(const Params &params, QcBlockPos base, double random, quint64 seed, const Put &put);
/*! Builds the feature with \a params, \a base, \a random and \a seed into \a chunk as build()
    does, keeping the blocks inside the chunk and the world height; blocks marked
    \c {onlyIntoAir} only where mayReplace() allows. */
void buildInChunk(QcChunk &chunk, const Params &params, QcBlockPos base, double random, quint64 seed);

} // namespace QcTreeShape
