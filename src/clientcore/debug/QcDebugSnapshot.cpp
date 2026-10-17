#include "clientcore/debug/QcDebugSnapshot.h"

#include "clientcore/session/QcClientSession.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcProcessMemory.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcProtocol.h"

#include <cmath>

namespace {

QcDebugSnapshot::Target targetOf(const QcClientWorld &world, const QcRaycastHit &hit)
{
    const QcContentRegistry &content = world.content();
    QcDebugSnapshot::Target target;
    target.pos = hit.block;
    target.face = hit.face;

    const std::optional<QcBlockId> id = world.blockAt(hit.block);
    if (id && content.isValidBlock(*id)) {
        const QcBlockDefinition &definition = content.block(*id);
        target.block = definition.name;
        for (const QcBlockProperty &property : definition.properties) {
            target.state.append({property.name, content.property(*id, property.name)});
        }
        // Blocks carry the tags of the item that places them (recipes and tools ask for those).
        if (const std::optional<QcItemId> item = content.itemId(definition.name)) {
            target.tags = content.item(*item).tags;
        }
    }

    const QcFaceOffset offset = qcFaceOffset(hit.face);
    const QcBlockPos front = hit.block.offset(offset.dx, offset.dy, offset.dz);
    target.light = QcLightProbe(world, front).at(front);
    return target;
}

QcDebugSnapshot::Entities countEntities(const QcClientSession &session)
{
    QcDebugSnapshot::Entities counts;
    for (const QcClientEntity &entity : session.entities()) {
        if (entity.isItem()) {
            ++counts.items;
        } else if (entity.isFallingBlock()) {
            ++counts.fallingBlocks;
        } else if (entity.isPlayer()) {
            ++counts.players;
        } else {
            ++counts.mobs;
        }
    }
    return counts;
}

QString targetEntityName(const QcClientSession &session)
{
    const std::optional<quint32> id = session.targetEntity();
    if (!id) {
        return {};
    }

    const QcClientEntity entity = session.entities().value(*id);
    const QcMobDefinition *creature = entity.creature(*session.content());
    const QString name = creature ? creature->name : entity.name;
    return QStringLiteral("%1 #%2").arg(name).arg(*id);
}

} // namespace

QcBlockPos QcDebugSnapshot::block() const
{
    return QcPhysics::blockContaining(position);
}

int QcDebugSnapshot::facingIndex(float yaw)
{
    const double turned = std::fmod(std::fmod(double(yaw), 360.0) + 360.0 + 45.0, 360.0);
    return int(turned / 90.0) % 4;
}

const char *QcDebugSnapshot::facing(float yaw)
{
    static constexpr std::array<const char *, 4> names{"north", "east", "south", "west"};
    return names[std::size_t(facingIndex(yaw))];
}

QcDebugSnapshot QcDebugSnapshot::collect(const QcClientSession &session, const std::optional<QcVec3> &feet)
{
    QcDebugSnapshot snapshot;
    snapshot.clientTick = session.tickCount();
    snapshot.protocol = QcProtocol::version;
    snapshot.worldName = session.worldName();
    snapshot.seed = session.seed();
    snapshot.position = feet.value_or(session.playerMotion().position);
    snapshot.yaw = session.intent().yaw;
    snapshot.pitch = session.intent().pitch;
    snapshot.memoryMb = QcProcessMemory::currentMb();
    snapshot.peakMemoryMb = QcProcessMemory::peakMb();
    snapshot.server = session.debug().mirror().latest();

    const QcClientWorld *world = session.world();
    if (!world) {
        return snapshot;
    }

    snapshot.loadedChunks = world->chunkCount();
    snapshot.entities = countEntities(session);
    snapshot.targetEntity = targetEntityName(session);
    const QcBlockPos feetBlock = snapshot.block();
    snapshot.feetLight = QcLightProbe(*world, feetBlock).at(feetBlock);
    if (const std::optional<QcRaycastHit> &hit = session.target()) {
        snapshot.target = targetOf(*world, *hit);
    }
    return snapshot;
}
