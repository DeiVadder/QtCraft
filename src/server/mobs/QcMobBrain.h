#pragma once

#include "common/physics/QcPhysics.h"
#include "server/mobs/QcMobManager.h"
#include "server/pathing/QcPathTerrain.h"

#include <QList>

class QcServerWorld;
struct QcCreature;
struct QcMobDefinition;

// What decides what a creature does (decision E-423), one of each kind of AI
// (QcMobDefinition::ai) in QcMobBehaviour, shared by all creatures with that AI: their state is in
// the creature (QcCreature, its components). QcMobBehaviour keeps what all of them share: the tiers
// of a crowd, the push of the crowd, the physics and flight. A new kind of creature (golems, tamed
// animals, archers) brings its own brain instead of a branch in QcMobBehaviour.
class QcMobBrain
{
public:
    // What one tick of all creatures shares, with the path rules of the creature at hand.
    struct Context
    {
        QcServerWorld &world;
        const QList<QcCreature> &mobs;
        double worldTime = 0.0;
        double dt = 0.0;
        QcPathRules rules;
        const QList<QcMobManager::PlayerView> *players = nullptr;
        QList<QcMobManager::PlayerHit> *hits = nullptr;
        // Ticks since the start: creatures look for players in turns, staggered by their ids.
        quint64 tick = 0;
    };

    QcMobBrain() = default;
    QcMobBrain(const QcMobBrain &) = delete;
    QcMobBrain &operator=(const QcMobBrain &) = delete;
    virtual ~QcMobBrain() = default;

    // Asked for a creature that does not stand (QcPose::Lying, a villager in its bed; Sitting): it
    // stays so without physics while this says so; otherwise it thinks this tick, in the pose the
    // brain leaves it in (the villagers' brain gets a villager up).
    virtual bool rest(QcCreature & /*mob*/, const Context & /*context*/) { return false; }
    // Decides what to do and how to move this tick. A creature in a crowd thinks every `thinkTicks`
    // ticks (1: every tick) and may then not search a path (QcMobCrowd::Tier).
    virtual QcPhysics::Steering think(QcCreature &mob, const QcMobDefinition &definition, const Context &context, int thinkTicks) = 0;
    // Whether it still thinks, in the slowest turn, in a crowd so dense that the others stand
    // (villagers: a crowd that met at the bell goes home at night).
    virtual bool thinksInIdleCrowd() const { return false; }
    // The creature leaves the world for good (died, removed, despawned; not unloaded).
    virtual void leave(const QcCreature & /*mob*/) {}
};
