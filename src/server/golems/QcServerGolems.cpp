#include "server/golems/QcServerGolems.h"

#include "common/physics/QcPhysics.h"
#include "server/blocks/QcServerBlocks.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/gameplay/QcServerCombat.h"
#include "server/gameplay/QcServerInventory.h"

#include <algorithm>

namespace {

// Whether the block at `pos` is loaded and one of `name`'s states.
bool isBlock(const QcBlockAccess &world, QcBlockPos pos, const QString &name)
{
    const std::optional<QcBlockId> block = world.blockAt(pos);
    return block && world.content().isValidBlock(*block) && world.content().block(*block).name == name;
}

bool isAir(const QcBlockAccess &world, QcBlockPos pos)
{
    return world.blockAt(pos) == qcAirBlockId;
}

} // namespace

QcServerGolems::QcServerGolems(QcServerState &state, QcServerSounds &sounds, QcServerBlocks &blocks, QcServerInventory &inventory)
    : m_state(state)
    , m_sounds(sounds)
    , m_blocks(blocks)
    , m_inventory(inventory)
{
}

std::optional<QcServerGolems::Shape> QcServerGolems::shapeAt(const QcBlockAccess &world, QcBlockPos head, const QcMobDefinition &definition)
{
    const QString &body = definition.golem.buildBody;
    const QcBlockPos middle = head.offset(0, -1, 0);
    const QcBlockPos foot = head.offset(0, -2, 0);
    if (body.isEmpty() || !isBlock(world, middle, body) || !isBlock(world, foot, body)) {
        return std::nullopt;
    }

    // The arms along x or along z; beside the head and the foot only air (the reference's pattern).
    for (const auto &[dx, dz] : {std::pair{1, 0}, std::pair{0, 1}}) {
        const QcBlockPos left = middle.offset(-dx, 0, -dz);
        const QcBlockPos right = middle.offset(dx, 0, dz);
        const bool arms = isBlock(world, left, body) && isBlock(world, right, body);
        const bool clear = isAir(world, head.offset(-dx, 0, -dz)) && isAir(world, head.offset(dx, 0, dz)) && isAir(world, foot.offset(-dx, 0, -dz))
                           && isAir(world, foot.offset(dx, 0, dz));
        if (arms && clear) {
            return Shape{head, foot, {head, middle, foot, left, right}};
        }
    }
    return std::nullopt;
}

std::optional<quint32> QcServerGolems::build(QcBlockPos pos)
{
    const QcContentRegistry &content = m_state.content();
    const std::optional<QcBlockId> placed = m_state.world().blockAt(pos);
    if (!placed || !content.isValidBlock(*placed)) {
        return std::nullopt;
    }

    const QString &name = content.block(*placed).name;
    for (int type = 0; type < static_cast<int>(content.mobs().size()); ++type) {
        const QcMobDefinition &definition = content.mobs()[type];
        if (definition.ai != QcMobDefinition::Ai::Golem || definition.golem.buildHead != name) {
            continue;
        }
        const std::optional<Shape> shape = shapeAt(m_state.world(), pos, definition);
        if (!shape) {
            continue;
        }

        // The blocks give their life to the golem: they vanish without drops.
        for (const QcBlockPos cell : shape->cells) {
            m_blocks.setAndBroadcast(cell, qcAirBlockId);
        }
        const QcVec3 feet{shape->foot.x + 0.5, static_cast<double>(shape->foot.y), shape->foot.z + 0.5};
        const quint32 id = m_state.entities().creatures().spawn(type, feet, m_state.entities().newId(), false, true);
        if (QcCreature *golem = m_state.entities().creatures().find(id); golem && golem->golem) {
            golem->golem->playerBuilt = true;
        }
        m_sounds.play(definition.hurtSound, feet + QcVec3{0.0, definition.height * 0.5, 0.0});
        return id;
    }
    return std::nullopt;
}

bool QcServerGolems::repair(QcServerClient &client, quint32 entityId, const QcItemDefinition *held)
{
    QcCreature *mob = m_state.entities().creatures().find(entityId);
    if (!mob || !held || client.dead) {
        return false;
    }
    const QcMobDefinition &definition = m_state.content().mobs()[mob->type];
    if (definition.golem.repairItem.isEmpty() || definition.golem.repairItem != held->name || mob->health >= definition.maxHealth) {
        return false;
    }

    const QcVec3 eye = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const QcVec3 centre = mob->motion.position + QcVec3{0.0, definition.height * 0.5, 0.0};
    if ((centre - eye).length() > QcServerCombat::attackReach + definition.width + 1.0
        || !QcLineOfSight::canSee(m_state.world(), client, QcPhysics::boxAt(mob->motion.position, definition.width, definition.height))) {
        return false;
    }

    mob->health = std::min(definition.maxHealth, mob->health + definition.golem.repairHealth);
    m_sounds.play(definition.golem.repairSound, centre);
    if (!client.rules().infiniteItems) {
        client.inventory.removeFromSlot(client.inventory.selectedHotbarSlot(), 1);
    }
    m_inventory.send(client);
    return true;
}
