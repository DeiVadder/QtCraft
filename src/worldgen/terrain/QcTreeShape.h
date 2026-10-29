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

// Block layout of tree and column features, shared by world generation and growing saplings so
// that grown trees look like generated ones.
namespace QcTreeShape {

// Crown of a tree (decision E-822): the small round and cone crowns, a bush of leaves around a
// single log, and the giant trees standing on a 2×2 trunk (a tall spruce with a layered cone, a
// jungle giant with branches and a wide flat crown).
enum class Crown : quint8 { Round, Cone, Bush, GiantCone, GiantRound };

struct Params
{
    QcFeatureDefinition::Type type = QcFeatureDefinition::Type::Tree;
    QcBlockId trunk = qcAirBlockId;
    // The trunk lying along x and z (branches of jungle giants); the trunk itself when it has no axis.
    QcBlockId trunkX = qcAirBlockId;
    QcBlockId trunkZ = qcAirBlockId;
    QcBlockId leaves = qcAirBlockId;
    int minHeight = 4;
    int maxHeight = 6;
    Crown crown = Crown::Round;
    // Vines hung on the trunk and the lowest leaves, one state per side the vine holds on to (north,
    // east, south, west); air when the tree has none.
    std::array<QcBlockId, 4> vines{};

    // Giant trees stand on four trunk blocks: `base` and the blocks east, south and south-east of it.
    bool giant() const { return crown == Crown::GiantCone || crown == Crown::GiantRound; }
};

// Receives every block of the feature; `onlyIntoAir` marks blocks (leaves, vines) that may replace
// only what mayReplace() allows. Returns whether the block was placed (or, outside the area the
// receiver writes, would be): vines hang only on blocks that were.
using Put = std::function<bool(QcBlockPos pos, QcBlockId block, bool onlyIntoAir)>;

// Whether `block` of a tree with `params` may go where `existing` is: anything for trunks, air for
// leaves and vines, and for leaves also the vines of another tree (a vine of this one may then hold
// on to them).
[[nodiscard]] bool mayReplace(const Params &params, QcBlockId existing, QcBlockId block, bool onlyIntoAir);

// The crown named by a feature's `shape` ("round", "cone", "bush", "giant_cone", "giant_round").
std::optional<Crown> crownOf(const QString &shape);
// The shape of `feature` with the blocks of `content` (air for unknown blocks).
Params paramsOf(const QcContentRegistry &content, const QcFeatureDefinition &feature);
// The vine states of the block `vine` (air for air), in the order of Params::vines.
std::array<QcBlockId, 4> vinesOf(const QcContentRegistry &content, QcBlockId vine);
// The trunk lying along x and along z, as Params::trunkX and trunkZ.
std::pair<QcBlockId, QcBlockId> lyingTrunks(const QcContentRegistry &content, QcBlockId trunk);

// How many blocks the feature reaches sideways from `base` (at most, for every height).
int reach(const Params &params);
// Trunk height for a `random` value in [0, 1).
int height(const Params &params, double random);
// Emits the feature standing on `base` (the lowest trunk block; of a giant the north-west one).
// `seed` decides the ragged corners of crowns, the branches and the vines.
void build(const Params &params, QcBlockPos base, double random, quint64 seed, const Put &put);
// Builds the feature into `chunk` as build() does, keeping the blocks inside the chunk and the world
// height; blocks marked `onlyIntoAir` only where mayReplace() allows.
void buildInChunk(QcChunk &chunk, const Params &params, QcBlockPos base, double random, quint64 seed);

} // namespace QcTreeShape
