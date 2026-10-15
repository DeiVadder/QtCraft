#include "QcBlockRules.h"

#include "QcContentRegistry.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace QcBlockRules {

namespace {

constexpr std::array<QcBlockFace, 4> horizontal = {QcBlockFace::North, QcBlockFace::East, QcBlockFace::South, QcBlockFace::West};

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

// The direction to the right of someone looking towards `face` (clockwise seen from above).
QcBlockFace rightOf(QcBlockFace face)
{
    const auto it = std::ranges::find(horizontal, face);
    return it == horizontal.end() ? face : horizontal[std::size_t((it - horizontal.begin() + 1) % std::ssize(horizontal))];
}

bool hasProperty(const QcContentRegistry &content, QcBlockId id, const QString &property)
{
    return content.block(id).property(property) != nullptr;
}

// Sets a property when the block has it and the value is allowed; false when the value is not allowed.
bool assign(const QcContentRegistry &content, QcBlockId &id, const QString &property, const QString &value)
{
    const QcBlockProperty *definition = content.block(id).property(property);
    if (!definition) {
        return true;
    }

    if (!definition->values.contains(value)) {
        return false;
    }

    id = content.withProperty(id, property, value);
    return true;
}

// A solid block whose side towards `towards` is completely covered.
bool hasSolidFace(const QcBlockAccess &world, QcBlockPos pos, QcBlockFace towards)
{
    const std::optional<QcBlockId> block = world.blockAt(pos);
    const QcContentRegistry &content = world.content();
    return block && content.isValidBlock(*block) && content.block(*block).solid
           && content.state(*block).fullFace[std::size_t(towards)];
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

    return other.solid && content.state(*neighbour).fullCube && other.renderLayer != QcRenderLayer::Invisible;
}

} // namespace

QString lookDirection(float yaw)
{
    const int quarter = int(std::lround(double(yaw) / 90.0)) % 4;
    return faceName(horizontal[std::size_t((quarter + 4) % 4)]);
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

// A slab clicked on its open side, or placed into a slab of the other half, doubles up.
[[nodiscard]] std::optional<Placement> doubledSlab(const QcBlockAccess &world, QcBlockId state, QcBlockPos clicked, QcBlockFace face,
                                     bool upper)
{
    const QcContentRegistry &content = world.content();
    const std::optional<QcBlockId> slabBelow = world.blockAt(clicked);
    if (slabBelow && content.isSameBlock(*slabBelow, state)) {
        const QString type = content.property(*slabBelow, u"type"_s);
        if ((type == u"bottom"_s && face == QcBlockFace::Up) || (type == u"top"_s && face == QcBlockFace::Down)) {
            return Placement{clicked, content.withProperty(state, u"type"_s, u"double"_s)};
        }
    }

    const QcBlockPos target = qcNeighbour(clicked, face);
    const std::optional<QcBlockId> existing = world.blockAt(target);
    if (existing && content.isSameBlock(*existing, state)) {
        const QString type = content.property(*existing, u"type"_s);
        if ((type == u"bottom"_s && upper) || (type == u"top"_s && !upper)) {
            return Placement{target, content.withProperty(state, u"type"_s, u"double"_s)};
        }
    }

    return std::nullopt;
}

// `state` with facing, slab type, half and attach set from the view and the clicked face;
// std::nullopt when one of the values is not allowed.
[[nodiscard]] std::optional<QcBlockId> orientedState(const QcContentRegistry &content, QcBlockId state, const QString &facing,
                                       QcBlockFace face, bool upper)
{
    const QcBlockDefinition &definition = content.block(state);
    const QString half = upper ? u"top"_s : u"bottom"_s;
    const QcBlockProperty *slabType = definition.property(u"type"_s);

    if (!assign(content, state, u"facing"_s, facing) || (slabType && !assign(content, state, u"type"_s, half))) {
        return std::nullopt;
    }

    if (!definition.door && !assign(content, state, u"half"_s, half)) {
        return std::nullopt;
    }

    if (hasProperty(content, state, u"attach"_s)) {
        // Towards the block the new one hangs on: the clicked block.
        const QcBlockFace towards = opposite(face);
        const QString attach = towards == QcBlockFace::Down ? u"floor"_s : (towards == QcBlockFace::Up ? u"ceiling"_s : faceName(towards));
        if (!assign(content, state, u"attach"_s, attach)) {
            return std::nullopt;
        }
    }

    return state;
}

// The lower half of a door at `target` with its hinge; std::nullopt without room for the upper half.
[[nodiscard]] std::optional<QcBlockId> doorState(const QcBlockAccess &world, QcBlockId state, QcBlockPos target, const QcVec3 &hit,
                                   const QString &facing)
{
    const QcContentRegistry &content = world.content();
    if (!replaceableAt(world, target.offset(0, 1, 0)) || !assign(content, state, u"half"_s, u"bottom"_s)) {
        return std::nullopt;
    }

    // The hinge sits on the side of the door the player aimed at.
    const QcFaceOffset right = qcFaceOffset(rightOf(faceFromName(facing).value_or(QcBlockFace::North)));
    const double along = (hit.x - 0.5) * right.dx + (hit.z - 0.5) * right.dz;
    assign(content, state, u"hinge"_s, along < 0.0 ? u"left"_s : u"right"_s);

    return state;
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

    const QcBlockProperty *slabType = definition.property(u"type"_s);
    if (slabType && slabType->values.contains(u"double"_s)) {
        if (std::optional<Placement> doubled = doubledSlab(world, state, clicked, face, upper)) {
            return doubled;
        }
        // A slab of the same half is already there.
        const std::optional<QcBlockId> existing = world.blockAt(qcNeighbour(clicked, face));
        if (existing && content.isSameBlock(*existing, state)) {
            return std::nullopt;
        }
    }

    const QcBlockPos target = qcNeighbour(clicked, face);
    if (!replaceableAt(world, target)) {
        return std::nullopt;
    }

    const QString facing = facingFor(definition, aim.yaw, aim.pitch, face);
    std::optional<QcBlockId> oriented = orientedState(content, state, facing, face, upper);
    if (oriented && definition.door) {
        oriented = doorState(world, *oriented, target, aim.hit, facing);
    }

    if (!oriented) {
        return std::nullopt;
    }

    state = connectedState(world, target, *oriented);
    // A door's upper half does not exist yet: the lower one only needs the floor.
    const bool supported = definition.door ? hasSolidFace(world, target.offset(0, -1, 0), QcBlockFace::Up)
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
        assign(content, state, side, connected[i] ? u"true"_s : u"false"_s);
    }

    // A wall shows its post except in the middle of a straight run.
    const bool straight = (connected[0] && connected[2] && !connected[1] && !connected[3])
                          || (connected[1] && connected[3] && !connected[0] && !connected[2]);
    assign(content, state, u"up"_s, straight ? u"false"_s : u"true"_s);
    return state;
}

bool isSupported(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id)
{
    const QcContentRegistry &content = world.content();
    if (!content.isValidBlock(id)) {
        return true;
    }

    const QcBlockDefinition &definition = content.block(id);
    if (definition.door) {
        if (!hasDoorPartner(world, pos, id)) {
            return false;
        }
        const bool top = content.property(id, u"half"_s) == u"top"_s;
        return top || !world.blockAt(pos.offset(0, -1, 0)) || hasSolidFace(world, pos.offset(0, -1, 0), QcBlockFace::Up);
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
            return hasSolidFace(world, below, QcBlockFace::Up);
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
        return !world.blockAt(holder) || hasSolidFace(world, holder, opposite(*towards));
    }
    }
    return true;
}

bool hasDoorPartner(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id)
{
    const QcContentRegistry &content = world.content();
    const std::optional<QcBlockId> partner = world.blockAt(otherDoorHalf(world, pos, id));
    if (!partner) {
        return true;
    }

    return content.isValidBlock(*partner) && content.isSameBlock(*partner, id)
           && content.property(*partner, u"half"_s) != content.property(id, u"half"_s);
}

QcBlockPos otherDoorHalf(const QcBlockAccess &world, QcBlockPos pos, QcBlockId id)
{
    return pos.offset(0, world.content().property(id, u"half"_s) == u"top"_s ? -1 : 1, 0);
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

} // namespace QcBlockRules
