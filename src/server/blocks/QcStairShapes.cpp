#include "server/blocks/QcStairShapes.h"

#include "common/content/registry/QcContentRegistry.h"
#include "server/blocks/QcBlockRules.h"

using namespace Qt::StringLiterals;

namespace QcStairShapes {

namespace {

struct Stair
{
    QcBlockFace facing = QcBlockFace::North;
    QString half;
};

// What lies next to a stair: nothing known (not loaded), no stairs, or stairs facing somewhere.
struct Neighbour
{
    bool loaded = false;
    std::optional<Stair> stair;
};

std::optional<Stair> stairOf(const QcContentRegistry &content, QcBlockId id)
{
    if (!content.isValidBlock(id) || !content.block(id).stairs) {
        return std::nullopt;
    }
    const std::optional<QcBlockFace> facing = QcBlockRules::faceFromName(content.property(id, u"facing"_s));
    if (!facing) {
        return std::nullopt;
    }
    return Stair{*facing, content.property(id, u"half"_s)};
}

Neighbour neighbourOf(const QcBlockAccess &world, QcBlockPos pos, QcBlockFace side)
{
    const std::optional<QcBlockId> block = world.blockAt(qcNeighbour(pos, side));
    if (!block) {
        return {};
    }
    return {true, stairOf(world.content(), *block)};
}

bool isAcross(QcBlockFace a, QcBlockFace b)
{
    return a != b && a != QcBlockRules::opposite(b);
}

// Whether the neighbour on `side` lets the stair take a corner: anything but the same stair (same
// facing and half) continuing a straight run; std::nullopt when it is not loaded.
std::optional<bool> mayTurn(const QcBlockAccess &world, QcBlockPos pos, const Stair &self, QcBlockFace side)
{
    const Neighbour neighbour = neighbourOf(world, pos, side);
    if (!neighbour.loaded) {
        return std::nullopt;
    }
    return !neighbour.stair || neighbour.stair->facing != self.facing || neighbour.stair->half != self.half;
}

// The shape of `self` at `pos`; std::nullopt while a deciding neighbour is not loaded.
std::optional<QString> shapeOf(const QcBlockAccess &world, QcBlockPos pos, const Stair &self)
{
    // Seen climbing up, the left is counter-clockwise of the facing.
    const QcBlockFace left = QcBlockRules::opposite(QcBlockRules::rightOf(self.facing));

    const Neighbour front = neighbourOf(world, pos, self.facing);
    if (!front.loaded) {
        return std::nullopt;
    }
    if (front.stair && front.stair->half == self.half && isAcross(front.stair->facing, self.facing)) {
        const std::optional<bool> turns = mayTurn(world, pos, self, QcBlockRules::opposite(front.stair->facing));
        if (!turns) {
            return std::nullopt;
        }
        if (*turns) {
            return front.stair->facing == left ? u"outer_left"_s : u"outer_right"_s;
        }
    }

    const Neighbour back = neighbourOf(world, pos, QcBlockRules::opposite(self.facing));
    if (!back.loaded) {
        return std::nullopt;
    }
    if (back.stair && back.stair->half == self.half && isAcross(back.stair->facing, self.facing)) {
        const std::optional<bool> turns = mayTurn(world, pos, self, back.stair->facing);
        if (!turns) {
            return std::nullopt;
        }
        if (*turns) {
            return back.stair->facing == left ? u"inner_left"_s : u"inner_right"_s;
        }
    }
    return u"straight"_s;
}

} // namespace

QcBlockId shapedState(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id)
{
    const QcContentRegistry &content = world.content();
    const std::optional<Stair> self = stairOf(content, id);
    if (!self) {
        return id;
    }

    const std::optional<QString> shape = shapeOf(world, pos, *self);
    return shape ? content.withProperty(id, u"shape"_s, *shape) : id;
}

} // namespace QcStairShapes
