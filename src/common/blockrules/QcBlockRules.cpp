#include "common/blockrules/QcBlockRules.h"

#include "common/blockrules/QcRails.h"
#include "common/blockrules/QcSlabRules.h"
#include "common/blockrules/QcStairShapes.h"
#include "common/content/registry/QcContentRegistry.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace QcBlockRules {

namespace {

constexpr std::array<QcBlockFace, 4> horizontal = {QcBlockFace::North, QcBlockFace::East, QcBlockFace::South, QcBlockFace::West};

bool hasProperty(const QcContentRegistry &content, QcBlockId id, const QString &property)
{
    return content.block(id).property(property) != nullptr;
}

// `id` with the property set when the block has it (unchanged otherwise); std::nullopt when the
// value is not allowed.
[[nodiscard]] std::optional<QcBlockId> assigned(const QcContentRegistry &content, QcBlockId id, const QString &property, const QString &value)
{
    const QcBlockProperty *definition = content.block(id).property(property);
    if (!definition) {
        return id;
    }

    if (!definition->values.contains(value)) {
        return std::nullopt;
    }
    return content.withProperty(id, property, value);
}

// A solid block whose side towards `towards` is completely covered.
bool hasSolidFace(const QcBlockAccess &world, QcBlockPos pos, QcBlockFace towards)
{
    const std::optional<QcBlockId> block = world.blockAt(pos);
    const QcContentRegistry &content = world.content();
    return block && content.isValidBlock(*block) && content.block(*block).solid
           && content.state(*block).fullFace[static_cast<std::size_t>(towards)];
}

// Whether `attached` is a signal attachment a piston base holds: a lever, a button, a torch (plain,
// small or signal) or dust (decision E-887). Rails, carpets, plates, snow, ladders, signs and
// lanterns keep the full-face rule.
bool isSignalAttachment(const QcContentRegistry &content, QcBlockId attached)
{
    if (!content.isValidBlock(attached)) {
        return false;
    }

    const QcBlockDefinition &definition = content.block(attached);
    switch (definition.signal.kind) {
    case QcSignalKind::Lever:
    case QcSignalKind::Button:
    case QcSignalKind::Torch:
    case QcSignalKind::Dust:
        return true;
    default:
        return definition.shapeName == u"qtcraft:torch"_s || definition.shapeName == u"qtcraft:small_torch"_s;
    }
}

// A piston base holds a signal attachment on any of its sides but its front (the head side), retracted
// or extended (decision E-887): its extended shape leaves the sides short of the front, but a lever on
// a piston must not fall off when the piston it switches extends. The front face is full only while
// retracted; it holds there by the full-face rule, not by this one.
bool pistonBaseHolds(const QcBlockAccess &world, QcBlockPos pos, QcBlockFace towards, QcBlockId attached)
{
    const std::optional<QcBlockId> block = world.blockAt(pos);
    const QcContentRegistry &content = world.content();
    if (!block || !content.isValidBlock(*block) || content.block(*block).signal.kind != QcSignalKind::Piston
        || !isSignalAttachment(content, attached)) {
        return false;
    }
    return faceFromName(content.property(*block, u"facing"_s)) != towards;
}

// Whether the block at `pos` holds `attached` resting on or hanging at its side `towards`: a full
// solid face, or for a signal attachment a piston base's side other than its front.
bool holdsOnFace(const QcBlockAccess &world, QcBlockPos pos, QcBlockFace towards, QcBlockId attached)
{
    return hasSolidFace(world, pos, towards) || pistonBaseHolds(world, pos, towards, attached);
}

// The axis a face lies across: "x" for east and west, "y" for up and down, "z" for north and south.
QString axisName(QcBlockFace face)
{
    const QcFaceOffset offset = qcFaceOffset(face);
    return offset.dx != 0 ? u"x"_s : (offset.dy != 0 ? u"y"_s : u"z"_s);
}

// Whether the block at `holder` holds `attached` at its side `side`: a solid block with that side
// completely covered (or a piston base, see holdsOnFace), or a block whose `axis` runs through that
// side, like the end of a chain (decision E-742).
bool holdsAttached(const QcBlockAccess &world, QcBlockPos holder, QcBlockFace side, QcBlockId attached)
{
    if (holdsOnFace(world, holder, side, attached)) {
        return true;
    }

    const std::optional<QcBlockId> block = world.blockAt(holder);
    const QcContentRegistry &content = world.content();
    return block && content.isValidBlock(*block) && hasProperty(content, *block, u"axis"_s)
           && content.property(*block, u"axis"_s) == axisName(side);
}

bool replaceableAt(const QcBlockAccess &world, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = world.blockAt(pos);
    return block && pos.isInsideWorldHeight() && world.content().isValidBlock(*block)
           && world.content().block(*block).replaceable;
}

bool connectsTo(const QcBlockAccess &world, const QcBlockDefinition &self, QcBlockPos neighbourPos)
{
    const std::optional<QcBlockId> neighbour = world.blockAt(neighbourPos);
    const QcContentRegistry &content = world.content();
    if (!neighbour || !content.isValidBlock(*neighbour) || *neighbour == qcAirBlockId) {
        return false;
    }

    const QcBlockDefinition &other = content.block(*neighbour);
    if (!other.connectGroup.isEmpty() && self.connectsTo.contains(other.connectGroup)) {
        return true;
    }

    return self.connectsToSolids && other.solid && content.state(*neighbour).fullCube && other.renderLayer != QcRenderLayer::Invisible;
}

} // namespace

[[nodiscard]] std::optional<QcBlockFace> faceFromName(const QString &name)
{
    for (const QcBlockFace face : qcAllBlockFaces) {
        if (faceName(face) == name) {
            return face;
        }
    }

    if (name == u"floor"_s) {
        return QcBlockFace::Down;
    }
    if (name == u"ceiling"_s) {
        return QcBlockFace::Up;
    }
    return std::nullopt;
}

QcBlockFace rightOf(QcBlockFace face)
{
    const auto it = std::ranges::find(horizontal, face);
    return it == horizontal.end() ? face : horizontal[static_cast<std::size_t>((it - horizontal.begin() + 1) % std::ssize(horizontal))];
}

QString lookDirection(float yaw)
{
    const int quarter = static_cast<int>(std::lround(static_cast<double>(yaw) / 90.0)) % 4;
    return faceName(horizontal[static_cast<std::size_t>((quarter + 4) % 4)]);
}

QString faceName(QcBlockFace face)
{
    switch (face) {
    case QcBlockFace::East:
        return u"east"_s;
    case QcBlockFace::West:
        return u"west"_s;
    case QcBlockFace::Up:
        return u"up"_s;
    case QcBlockFace::Down:
        return u"down"_s;
    case QcBlockFace::South:
        return u"south"_s;
    case QcBlockFace::North:
        return u"north"_s;
    }
    return {};
}

QcBlockFace opposite(QcBlockFace face)
{
    switch (face) {
    case QcBlockFace::East:
        return QcBlockFace::West;
    case QcBlockFace::West:
        return QcBlockFace::East;
    case QcBlockFace::Up:
        return QcBlockFace::Down;
    case QcBlockFace::Down:
        return QcBlockFace::Up;
    case QcBlockFace::South:
        return QcBlockFace::North;
    case QcBlockFace::North:
        return QcBlockFace::South;
    }
    return face;
}

namespace {

// The facing value a block placed while looking along `yaw` and `pitch` against the clicked `face`
// takes.
QString facingFor(const QcBlockDefinition &definition, float yaw, float pitch, QcBlockFace face)
{
    const QcBlockProperty *facing = definition.property(u"facing"_s);
    if (definition.facingRule == QcFacingRule::Against) {
        // Against the clicked block on a side face, else where the player looks (decision E-755).
        const bool side = face != QcBlockFace::Up && face != QcBlockFace::Down;
        return side ? faceName(opposite(face)) : lookDirection(yaw);
    }
    if (definition.facingRule == QcFacingRule::Into) {
        // Into the clicked block; where that is not allowed (a hopper under a ceiling), down.
        const QString into = faceName(opposite(face));
        return facing && facing->values.contains(into) ? into : u"down"_s;
    }

    const bool vertical = facing && facing->values.contains(u"up"_s) && facing->values.contains(u"down"_s);
    if (vertical && std::abs(pitch) > verticalFacingPitch) {
        const bool up = (pitch > 0.0F) == (definition.facingRule == QcFacingRule::Away);
        return up ? u"up"_s : u"down"_s;
    }

    const QString look = lookDirection(yaw);
    const QcBlockFace lookFace = faceFromName(look).value_or(QcBlockFace::North);
    return definition.facingRule == QcFacingRule::Toward ? faceName(opposite(lookFace)) : look;
}

// What placing a slab or a vertical slab does before the other rules: it doubles one up, a slab of
// the same half is in the way, or neither.
struct SlabOutcome
{
    std::optional<Placement> doubled;
    bool blocked = false;
};

// `aim` with the clicked block and face as the placement uses them (below tall grass: its ground).
SlabOutcome slabOutcome(const QcBlockAccess &world, QcBlockId state, const Aim &aim)
{
    const QcContentRegistry &content = world.content();
    const QcBlockDefinition &definition = content.block(state);
    if (QcSlabRules::isVertical(definition)) {
        const QString facing = facingFor(definition, aim.yaw, aim.pitch, aim.face);
        return {QcSlabRules::doubledVertical(world, state, aim.clicked, aim.face, facing)};
    }

    const QcBlockProperty *slabType = definition.property(u"type"_s);
    if (!slabType || !slabType->values.contains(u"double"_s)) {
        return {};
    }
    const bool upper = aim.face == QcBlockFace::Down || (aim.face != QcBlockFace::Up && aim.hit.y > 0.5);
    if (const std::optional<Placement> doubled = QcSlabRules::doubled(world, state, aim.clicked, aim.face, upper)) {
        return {doubled};
    }
    // A slab of the same half is already there.
    const std::optional<QcBlockId> existing = world.blockAt(qcNeighbour(aim.clicked, aim.face));
    return {std::nullopt, existing && content.isSameBlock(*existing, state)};
}

// `state` with facing, slab type, half and attach set from the view and the clicked face;
// std::nullopt when one of the values is not allowed.
[[nodiscard]] std::optional<QcBlockId> orientedState(const QcContentRegistry &content, QcBlockId state, const QString &facing,
                                       QcBlockFace face, bool upper)
{
    const QcBlockDefinition &definition = content.block(state);
    const QString half = upper ? u"top"_s : u"bottom"_s;
    const QcBlockProperty *slabType = definition.property(u"type"_s);

    std::optional<QcBlockId> oriented = assigned(content, state, u"facing"_s, facing);
    if (oriented && slabType) {
        oriented = assigned(content, *oriented, u"type"_s, half);
    }
    if (oriented && !definition.door) {
        oriented = assigned(content, *oriented, u"half"_s, half);
    }

    if (oriented && hasProperty(content, *oriented, u"attach"_s)) {
        // Towards the block the new one hangs on: the clicked block.
        const QcBlockFace towards = opposite(face);
        const QString attach = towards == QcBlockFace::Down ? u"floor"_s : (towards == QcBlockFace::Up ? u"ceiling"_s : faceName(towards));
        oriented = assigned(content, *oriented, u"attach"_s, attach);
    }

    // Chains (decision E-742) run along the axis of the clicked face.
    return oriented ? assigned(content, *oriented, u"axis"_s, axisName(face)) : oriented;
}

// `state` turned like a standing sign (decision E-766): its `rotation` (0..15) faces the player in
// sixteenths of a turn from north; on a wall (`attach` other than floor) it stays 0.
[[nodiscard]] std::optional<QcBlockId> rotatedState(const QcContentRegistry &content, QcBlockId state, float yaw)
{
    const QString attach = content.property(state, u"attach"_s);
    const bool standing = attach.isEmpty() || attach == u"floor"_s;
    const long steps = standing ? ((std::lround(static_cast<double>(yaw) / 22.5) % 16) + 16) % 16 : 0;
    return assigned(content, state, u"rotation"_s, QString::number(steps));
}

// The lower half of a door at `target` with its hinge; std::nullopt without room for the upper half.
[[nodiscard]] std::optional<QcBlockId> doorState(const QcBlockAccess &world, QcBlockId state, QcBlockPos target, const QcVec3 &hit,
                                   const QString &facing)
{
    const QcContentRegistry &content = world.content();
    const std::optional<QcBlockId> lower = replaceableAt(world, target.offset(0, 1, 0)) ? assigned(content, state, u"half"_s, u"bottom"_s)
                                                                                         : std::nullopt;
    if (!lower) {
        return std::nullopt;
    }

    // The hinge sits on the side of the door the player aimed at.
    const QcFaceOffset right = qcFaceOffset(rightOf(faceFromName(facing).value_or(QcBlockFace::North)));
    const double along = (hit.x - 0.5) * right.dx + (hit.z - 0.5) * right.dz;
    return assigned(content, *lower, u"hinge"_s, along < 0.0 ? u"left"_s : u"right"_s).value_or(*lower);
}

// The foot of a bed at `target` whose head goes one block further in `facing`; std::nullopt
// without room or floor for the head.
[[nodiscard]] std::optional<QcBlockId> bedState(const QcBlockAccess &world, QcBlockId state, QcBlockPos target, const QString &facing)
{
    const QcFaceOffset ahead = qcFaceOffset(faceFromName(facing).value_or(QcBlockFace::North));
    const QcBlockPos head = target.offset(ahead.dx, 0, ahead.dz);
    if (!replaceableAt(world, head) || !hasSolidFace(world, head.offset(0, -1, 0), QcBlockFace::Up)) {
        return std::nullopt;
    }
    return assigned(world.content(), state, u"part"_s, u"foot"_s);
}

// A door half needs its partner and the lower one a floor; a bed half only its partner (the floor
// is checked when it is placed).
bool pairSupported(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id)
{
    if (!hasPartner(world, pos, id)) {
        return false;
    }
    const QcContentRegistry &content = world.content();
    const bool top = content.property(id, u"half"_s) == u"top"_s;
    return content.block(id).bed || top || !world.blockAt(pos.offset(0, -1, 0)) || hasSolidFace(world, pos.offset(0, -1, 0), QcBlockFace::Up);
}

} // namespace

[[nodiscard]] std::optional<Placement> placement(const QcBlockAccess &world, QcBlockId block, const Aim &aim)
{
    const QcContentRegistry &content = world.content();
    if (!content.isValidBlock(block)) {
        return std::nullopt;
    }

    // A clicked replaceable block (tall grass) gives way: the new block takes its cell, as if placed
    // on the ground below it.
    QcBlockPos clicked = aim.clicked;
    const std::optional<QcBlockId> clickedBlock = world.blockAt(clicked);
    const bool intoClicked = clickedBlock && *clickedBlock != qcAirBlockId && content.isValidBlock(*clickedBlock)
                             && content.block(*clickedBlock).replaceable && !content.block(*clickedBlock).liquid
                             && !content.isSameBlock(*clickedBlock, block);
    if (intoClicked) {
        clicked = clicked.offset(0, -1, 0);
    }

    const QcBlockFace face = intoClicked ? QcBlockFace::Up : aim.face;
    QcBlockId state = content.stateTable().firstState(content.state(block).type);
    const QcBlockDefinition &definition = content.block(state);
    // Upper or lower half from the clicked face, or from the height of the hit on a side face.
    const bool upper = face == QcBlockFace::Down || (face != QcBlockFace::Up && aim.hit.y > 0.5);

    const SlabOutcome slab = slabOutcome(world, state, {clicked, face, aim.hit, aim.yaw, aim.pitch});
    if (slab.doubled || slab.blocked) {
        return slab.doubled;
    }

    const QcBlockPos target = qcNeighbour(clicked, face);
    if (!replaceableAt(world, target)) {
        return std::nullopt;
    }

    const QString facing = facingFor(definition, aim.yaw, aim.pitch, face);
    std::optional<QcBlockId> oriented = orientedState(content, state, facing, face, upper);
    if (oriented) {
        oriented = rotatedState(content, *oriented, aim.yaw);
    }
    if (oriented && definition.door) {
        oriented = doorState(world, *oriented, target, aim.hit, facing);
    }
    if (oriented && definition.bed) {
        oriented = bedState(world, *oriented, target, facing);
    }

    if (!oriented) {
        return std::nullopt;
    }

    // A rail lies along the rails around it, else along the player's view (decision E-382).
    state = definition.rail ? QcRails::shapedState(world, target, *oriented, faceFromName(lookDirection(aim.yaw)))
                            : connectedState(world, target, *oriented);
    // A door's upper half and a bed's head do not exist yet: the lower half and the foot only need
    // the floor.
    const bool supported = definition.door || definition.bed ? hasSolidFace(world, target.offset(0, -1, 0), QcBlockFace::Up)
                                                             : isSupported(world, target, state);
    if (!supported) {
        return std::nullopt;
    }

    return Placement{target, state};
}

QcBlockId connectedState(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id)
{
    const QcContentRegistry &content = world.content();
    if (!content.isValidBlock(id)) {
        return id;
    }
    const QcBlockDefinition &definition = content.block(id);
    if (definition.rail) {
        return QcRails::shapedState(world, pos, id, std::nullopt);
    }
    if (definition.stairs) {
        return QcStairShapes::shapedState(world, pos, id);
    }
    if (definition.connectsTo.isEmpty()) {
        return id;
    }

    QcBlockId state = id;
    std::array<bool, 4> connected{};
    for (std::size_t i = 0; i < horizontal.size(); ++i) {
        const QcBlockPos neighbour = qcNeighbour(pos, horizontal[i]);
        const QString side = faceName(horizontal[i]);
        // Towards an unloaded chunk the side stays as it is; loading the chunk updates it.
        connected[i] = world.blockAt(neighbour) ? connectsTo(world, definition, neighbour) : content.property(id, side) == u"true"_s;
        state = assigned(content, state, side, connected[i] ? u"true"_s : u"false"_s).value_or(state);
    }

    // A wall shows its post except in the middle of a straight run.
    const bool straight = (connected[0] && connected[2] && !connected[1] && !connected[3])
                          || (connected[1] && connected[3] && !connected[0] && !connected[2]);
    return assigned(content, state, u"up"_s, straight ? u"false"_s : u"true"_s).value_or(state);
}

bool isSupported(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id)
{
    const QcContentRegistry &content = world.content();
    if (!content.isValidBlock(id)) {
        return true;
    }

    const QcBlockDefinition &definition = content.block(id);
    if (definition.door || definition.bed) {
        return pairSupported(world, pos, id);
    }

    switch (definition.support) {
    case QcBlockSupport::None:
        return true;
    case QcBlockSupport::Below: {
        // Unloaded neighbours never break anything.
        const QcBlockPos below = pos.offset(0, -1, 0);
        const std::optional<QcBlockId> ground = world.blockAt(below);
        if (!ground) {
            return true;
        }

        if (definition.groundBlocks.isEmpty() && definition.groundLiquid.isEmpty()) {
            return holdsOnFace(world, below, QcBlockFace::Up, id) || (content.isValidBlock(*ground) && content.block(*ground).supportsAbove);
        }

        if (!content.isValidBlock(*ground)) {
            return false;
        }

        const bool groundOk = definition.groundBlocks.isEmpty() ? hasSolidFace(world, below, QcBlockFace::Up)
                                                                : definition.groundBlocks.contains(content.block(*ground).name);
        if (!groundOk || definition.groundLiquid.isEmpty() || content.isSameBlock(*ground, id)) {
            return groundOk;
        }

        // The liquid has to touch the ground block from a side (water next to the sand of a cane).
        return std::ranges::any_of(horizontal, [&](QcBlockFace face) {
            const std::optional<QcBlockId> side = world.blockAt(qcNeighbour(below, face));
            return !side || (content.isValidBlock(*side) && content.block(*side).name == definition.groundLiquid);
        });
    }
    case QcBlockSupport::Attached: {
        const std::optional<QcBlockFace> towards = faceFromName(content.property(id, u"attach"_s));
        if (!towards) {
            return true;
        }

        const QcBlockPos holder = qcNeighbour(pos, *towards);
        return !world.blockAt(holder) || holdsAttached(world, holder, opposite(*towards), id);
    }
    }
    return true;
}

bool hasPartner(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id)
{
    const QcContentRegistry &content = world.content();
    const std::optional<QcBlockId> partner = world.blockAt(otherHalf(world, pos, id));
    if (!partner) {
        return true;
    }

    const QString part = content.block(id).bed ? u"part"_s : u"half"_s;
    return content.isValidBlock(*partner) && content.isSameBlock(*partner, id) && content.property(*partner, part) != content.property(id, part)
           && (!content.block(id).bed || content.property(*partner, u"facing"_s) == content.property(id, u"facing"_s));
}

QcBlockPos otherHalf(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id)
{
    const QcContentRegistry &content = world.content();
    if (content.block(id).bed) {
        const QcFaceOffset ahead = qcFaceOffset(faceFromName(content.property(id, u"facing"_s)).value_or(QcBlockFace::North));
        const int sign = content.property(id, u"part"_s) == u"head"_s ? -1 : 1;
        return pos.offset(sign * ahead.dx, 0, sign * ahead.dz);
    }
    return pos.offset(0, content.property(id, u"half"_s) == u"top"_s ? -1 : 1, 0);
}

[[nodiscard]] std::optional<QcBlockId> hardened(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id)
{
    const QcContentRegistry &content = world.content();
    const QcBlockDefinition &definition = content.block(id);
    const std::optional<QcBlockId> into = definition.hardensWith.isEmpty() ? std::nullopt : content.blockId(definition.hardensInto);
    if (!into) {
        return std::nullopt;
    }

    for (const QcBlockFace face : qcAllBlockFaces) {
        const std::optional<QcBlockId> neighbour = world.blockAt(qcNeighbour(pos, face));
        if (neighbour && content.isValidBlock(*neighbour) && content.block(*neighbour).name == definition.hardensWith) {
            return into;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<QcBlockId> covered(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id)
{
    const QcContentRegistry &content = world.content();
    const QcBlockDefinition &definition = content.block(id);
    const std::optional<QcBlockId> into = definition.coveredInto.isEmpty() ? std::nullopt : content.blockId(definition.coveredInto);
    const std::optional<QcBlockId> above = world.blockAt(pos.offset(0, 1, 0));
    if (!into || !above || !content.isValidBlock(*above) || !content.state(*above).opaque) {
        return std::nullopt;
    }
    return into;
}

} // namespace QcBlockRules
