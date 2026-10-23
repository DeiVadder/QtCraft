#include "server/mobs/QcMobFamilies.h"

#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcServerCombat.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

QcMobFamilies::QcMobFamilies(QcServerState &state, QcServerSounds &sounds)
    : m_state(state)
    , m_sounds(sounds)
{
    m_departures = m_state.entities().creatures().onDeparture(
        [this](const QcCreature &mob, QcMobManager::Departure why) { noteDeparture(mob, why); });
}

QcMobFamilies::~QcMobFamilies()
{
    m_state.entities().creatures().removeDepartureListener(m_departures);
}

QList<quint32> QcMobFamilies::childrenOf(const QcMobManager &creatures, quint32 parentId)
{
    QList<quint32> children;
    for (const QcCreature &mob : creatures.mobs()) {
        if (mob.parent == parentId && mob.id != parentId) {
            children.append(mob.id);
        }
    }
    return children;
}

void QcMobFamilies::noteDeparture(const QcCreature &mob, QcMobManager::Departure why)
{
    const std::optional<QcFamilyDefinition> &family = m_state.content().mobs()[mob.type].family;
    if (!family) {
        return;
    }

    // A kept parent rests with its chunk: its children wait for it by its persistent id. The store
    // may not change while the listeners run, but the children's link may.
    QcMobManager &creatures = m_state.entities().creatures();
    if (why == QcMobManager::Departure::Unloaded && mob.persistent) {
        for (QcCreature &child : creatures.editableMobs()) {
            if (child.parent == mob.id) {
                child.parent = 0;
                child.parentChunk = QcPhysics::blockContaining(mob.motion.position).chunk();
            }
        }
        return;
    }

    // The others go in the next tick of the creatures, after the step that took the parent.
    Orphans orphans{childrenOf(creatures, mob.id), family->popDrops, family->popSound, why == QcMobManager::Departure::Died};
    if (!orphans.children.isEmpty()) {
        m_orphans.append(std::move(orphans));
    }
}

void QcMobFamilies::tick()
{
    takeOrphans();
    splitHurt();
    tendKept();
}

void QcMobFamilies::tendKept()
{
    QcMobManager &creatures = m_state.entities().creatures();
    bool waiting = false;
    for (QcCreature &child : creatures.editableMobs()) {
        if (child.parentUid == 0) {
            continue;
        }
        if (child.parent == 0) {
            waiting = true;
        } else if (const QcCreature *parent = creatures.find(child.parent)) {
            child.parentChunk = QcPhysics::blockContaining(parent->motion.position).chunk();
        }
    }
    if (!waiting) {
        return;
    }

    // A parent in the world takes its children back; one missing from the loaded chunk it was last
    // in died (or left) while they were unloaded, and they go on as ordinary monsters.
    creatures.relinkFamilies();
    for (QcCreature &child : creatures.editableMobs()) {
        if (child.parentUid != 0 && child.parent == 0 && child.parentChunk && m_state.world().chunk(*child.parentChunk)) {
            creatures.release(child);
        }
    }
}

void QcMobFamilies::takeOrphans()
{
    const QList<Orphans> orphans = std::exchange(m_orphans, {});
    QcMobManager &creatures = m_state.entities().creatures();
    int sounds = 0;
    for (const Orphans &family : orphans) {
        for (const quint32 id : family.children) {
            const QcCreature *child = creatures.find(id);
            if (!child) {
                continue;
            }
            const QcVec3 centre = child->motion.position + QcVec3{0.0, m_state.content().mobs()[child->type].height * 0.5, 0.0};
            creatures.remove(id);
            if (!family.pop) {
                continue;
            }

            // The clients see it go as at a death; the pop leaves the family's drops.
            m_state.entities().noteDeath(id);
            QcServerCombat::dropLoot(m_state, family.popDrops, centre);
            if (!family.popSound.isEmpty() && sounds++ < popSoundsPerTick) {
                m_sounds.play(family.popSound, centre);
            }
            ++m_popped;
        }
    }
}

void QcMobFamilies::splitHurt()
{
    // The parents and how many children each splits off; splitting adds creatures, so first all.
    QList<std::pair<quint32, int>> hurt;
    for (const QcCreature &mob : m_state.entities().creatures().mobs()) {
        const std::optional<QcFamilyDefinition> &family = m_state.content().mobs()[mob.type].family;
        // Only a fresh hit that took health splits: neither a snowball or an egg (no damage) nor a
        // stronger hit during the invulnerability does.
        if (family && mob.woundedThisTick && !mob.dormant && mob.health > 0.0) {
            hurt.append({mob.id, family->childrenPerHit});
        }
    }
    for (const auto &[id, count] : std::as_const(hurt)) {
        split(m_state, id, count);
    }
}

QList<quint32> QcMobFamilies::split(QcServerState &state, quint32 parentId, int count)
{
    QcMobManager &creatures = state.entities().creatures();
    const QcCreature *found = creatures.find(parentId);
    const QcMobDefinition *kind = found ? &state.content().mobs()[found->type] : nullptr;
    if (!kind || !kind->family || kind->family->childType < 0) {
        return {};
    }

    // A copy: spawning moves the creatures in the store.
    const QcCreature parent = *found;
    const QcFamilyDefinition &family = *kind->family;
    const qsizetype room = std::min(family.maxChildren - childrenOf(creatures, parentId).size(), static_cast<qsizetype>(family.maxSplits - parent.splits));
    QList<quint32> children;
    for (qsizetype i = 0; i < std::min(static_cast<qsizetype>(count), room); ++i) {
        // Within the parent's body, so the smaller child floats where the parent had room, and away
        // from its middle with a little leap.
        const double angle = state.random().bounded(2.0 * std::numbers::pi);
        const QcVec3 away{std::sin(angle), 0.0, -std::cos(angle)};
        const QcMobDefinition &childKind = state.content().mobs()[family.childType];
        const double reach = std::max(0.0, (kind->width - childKind.width) * 0.5);
        const QcVec3 middle{0.0, std::max(0.0, (kind->height - childKind.height) * 0.5), 0.0};
        const quint32 id = creatures.spawn(family.childType, parent.motion.position + middle + away * reach, state.entities().newId());

        QcCreature &child = *creatures.find(id);
        child.parent = parentId;
        child.confined = parent.confined;
        child.underground = parent.underground;
        child.motion.velocity = away * splitSpeed + QcVec3{0.0, splitLift, 0.0};
        if (parent.persistent) {
            creatures.keep(child, true);
            child.parentUid = parent.uid;
        }
        children.append(id);
    }
    creatures.find(parentId)->splits += static_cast<int>(children.size());
    return children;
}
