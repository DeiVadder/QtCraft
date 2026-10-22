#pragma once

#include "server/mobs/QcMobManager.h"

#include <QList>

class QcServerSounds;
class QcServerState;

// Parents and their children (QcMobDefinition::family, decision E-839; the Kjut), like a QObject
// and its children: a parent wounded and still alive splits off children of its family's kind (at
// most `maxChildren` of them live at once, `maxSplits` in its life); a child points to its parent
// (QcCreature::parent, and QcCreature::parentUid in a kept family). When a parent dies, every child
// of it pops in the next tick of the creatures, as QObject::deleteLater deletes later: it is gone
// with the family's pop sound and leaves the family's `popDrops` (no explosion, no block damage); a
// child killed on its own leaves only its own drops. When a parent leaves the world without a death (despawned, removed, fell
// into the void, or its chunk unloaded while it was not kept), its children vanish quietly with it.
// A kept parent whose chunk unloads rests with the chunk: its children stay and wait for it
// (QcCreature::parentUid; QcMobManager::relinkFamilies links them again when it loads); once the
// chunk it was last in is loaded without it (it died while they were unloaded), they are released
// as ordinary monsters. Children of a kept parent are kept too (QcMobManager::keep), those of a
// confined one confined.
class QcMobFamilies
{
public:
    // Pop sounds per tick at most (many families dying at once).
    static constexpr int popSoundsPerTick = 8;
    // A child split off leaps away from its parent with this horizontal speed and lift, in m/s; the
    // lift fades over a few ticks (QcMobHover::liftFade), so the child pops up.
    static constexpr double splitSpeed = 2.5;
    static constexpr double splitLift = 5.0;

    QcMobFamilies(QcServerState &state, QcServerSounds &sounds);
    ~QcMobFamilies();
    QcMobFamilies(const QcMobFamilies &) = delete;
    QcMobFamilies &operator=(const QcMobFamilies &) = delete;

    // Before the creatures move: the children of parents that left since the last call pop or
    // vanish, the parents wounded since their last move split, and kept children note where their
    // parent is or, waiting for one that is gone, are released.
    void tick();
    // Splits up to `count` children off the parent `parentId`, no more than its family's
    // `maxChildren` living at once and `maxSplits` in its life (counted in QcCreature::splits); the
    // ids of the new children (none: no such parent, no family, no splits left).
    static QList<quint32> split(QcServerState &state, quint32 parentId, int count);
    // The ids of the children of the parent `parentId` in the world.
    static QList<quint32> childrenOf(const QcMobManager &creatures, quint32 parentId);
    // Children that popped since the server started (tests, measurements).
    qint64 popped() const { return m_popped; }

private:
    // The children of a parent that left, and whether they pop (with the family's drops and sound)
    // or vanish.
    struct Orphans
    {
        QList<quint32> children;
        QList<QcItemDrop> popDrops;
        QString popSound;
        bool pop = false;
    };

    // Notes the children of a parent that leaves; a kept parent whose chunk unloads lets them wait.
    void noteDeparture(const QcCreature &mob, QcMobManager::Departure why);
    // The children of parents that left pop or vanish.
    void takeOrphans();
    // Splits children off the parents wounded since their last move.
    void splitHurt();
    // Kept children note the chunk of their parent; those waiting for a parent that is not in its
    // loaded chunk are released.
    void tendKept();

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcMobManager::DepartureToken m_departures{};
    QList<Orphans> m_orphans;
    qint64 m_popped = 0;
};
