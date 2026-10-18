#include "server/mobs/QcServerAnimals.h"

#include "common/content/definitions/QcSoundKeys.h"
#include "common/physics/QcPhysics.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcLineOfSight.h"
#include "server/gameplay/QcServerCombat.h"
#include "server/gameplay/QcServerInventory.h"
#include "server/items/QcServerExperience.h"
#include "server/mobs/QcMobLaying.h"

QcServerAnimals::QcServerAnimals(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory)
    : m_state(state)
    , m_sounds(sounds)
    , m_inventory(inventory)
    , m_breeding(state.contentPointer())
    , m_pets(state, sounds, inventory)
{
}

QcAabb QcServerAnimals::shownBox(const QcMobDefinition &definition, const QcCreature &mob)
{
    const double scale = mob.isYoung() ? QcMsg::youngScale : 1.0;
    return QcPhysics::boxAt(mob.motion.position, definition.width * scale, definition.height * scale);
}

QcServerAnimals::Feeding QcServerAnimals::feed(QcServerClient &client, quint32 mobId)
{
    QcCreature *mob = m_state.entities().creatures().find(mobId);
    const QcItemDefinition *held = m_state.heldItem(client);
    if (!mob || !held || mob->dormant || mob->type < 0 || mob->type >= m_state.content().mobs().size()
        || !m_state.content().mobs()[mob->type].breeding.food.contains(held->name)) {
        return Feeding::NotItsFood;
    }

    const QcMobDefinition &definition = m_state.content().mobs()[mob->type];
    const QcVec3 eye = client.motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const QcVec3 centre = mob->motion.position + QcVec3{0.0, definition.height * 0.5, 0.0};
    if ((centre - eye).length() > QcServerCombat::attackReach + definition.width + 1.0 || !QcLineOfSight::canSee(m_state.world(), client, shownBox(definition, *mob))
        || m_breeding.feed(*mob, held->name) == QcMobBreeding::Fed::No) {
        return Feeding::Refused;
    }

    if (!client.rules().infiniteItems) {
        client.inventory.removeFromSlot(client.inventory.selectedHotbarSlot(), 1);
    }
    m_inventory.send(client);
    m_sounds.play(QString::fromLatin1(QcSoundKeys::playerEat), centre, 0.6f);
    return Feeding::Fed;
}

quint32 QcServerAnimals::spawnYoung(QcServerState &state, int type, const QcVec3 &position)
{
    QcMobBreeding limits(state.contentPointer());
    limits.setLimits(state.config().limits.breedingCrowd, state.config().limits.bredPerKind);
    QcMobManager &creatures = state.entities().creatures();
    if (!limits.roomFor(creatures.mobs(), type, position)) {
        return 0;
    }

    const quint32 id = creatures.spawn(type, position, state.entities().newId(), false, true);
    if (QcCreature *young = creatures.find(id)) {
        young->growUp = state.content().mobs()[type].breeding.growUpSeconds;
        young->bred = true;
    }
    return id;
}

void QcServerAnimals::tick()
{
    m_breeding.setLimits(m_state.config().limits.breedingCrowd, m_state.config().limits.bredPerKind);
    const QList<QcMobBreeding::Birth> births = m_breeding.tick(m_state.world(), m_state.entities().creatures(), QcPhysics::tickDuration);
    for (const QcMobBreeding::Birth &birth : births) {
        // Bred animals are always kept with the world.
        const quint32 id = m_state.entities().creatures().spawn(birth.type, birth.position, m_state.entities().newId(), false, true);
        if (QcCreature *young = m_state.entities().creatures().find(id)) {
            young->growUp = m_state.content().mobs()[birth.type].breeding.growUpSeconds;
            young->bred = true;
        }
        m_sounds.play(m_state.content().mobs()[birth.type].ambientSound, birth.position);
        // The parents' reward, as in the reference (decision E-398).
        QcServerExperience::drop(m_state, QcServerExperience::roll(m_state, m_state.content().mobs()[birth.type].breeding.experience), birth.position);
    }

    layEggs();
}

void QcServerAnimals::layEggs()
{
    QcEntities &entities = m_state.entities();
    const QList<QcMobLaying::Laid> laid = QcMobLaying::tick(m_state.content(), entities.creatures().editableMobs(), m_state.random(), QcPhysics::tickDuration);
    for (const QcMobLaying::Laid &egg : laid) {
        // Like a block's drop (decision E-028): scattered a little from its feet.
        if (const std::optional<QcItemId> item = m_state.content().itemId(egg.item)) {
            entities.items().spawnScattered({*item, 1, 0}, egg.position + QcVec3{0.0, 0.25, 0.0}, entities.newId());
        }
        if (!egg.sound.isEmpty()) {
            m_sounds.play(egg.sound, egg.position);
        }
    }
}
