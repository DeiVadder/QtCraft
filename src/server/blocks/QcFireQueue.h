#pragma once

#include "common/world/QcChunk.h"
#include "server/core/QcServerLimits.h"

#include <QHash>
#include <QList>
#include <QSet>

#include <map>
#include <optional>
#include <utility>
#include <vector>

// The burning fires of the loaded world and when each takes its next step (decision E-392): one
// entry per fire block, ordered by due tick, so a tick only looks at the fires that are due and costs
// the same in a world of any size. Kept in step with every block set (QcServerWorld::setBlock): a
// fire block that goes away loses its entry, a fire block set without one (a command, a player, a
// chunk whose entries were lost) waits in a list until QcServerFire adopts or puts it out. Entries
// leave and come back with their chunk (decision E-374), with the ticks left to their step. A fire on
// eternal ground (a block with "eternalFire") never ages and never spreads; it does not count toward
// the caps per chunk and world (decision E-394).
class QcFireQueue
{
public:
    // Steps a fire lives at most; every fire but an eternal one is out after them. The chances of
    // the rules take a third of the age, the reference game's age of 0 to 15 (it ages by one in
    // about three steps there).
    static constexpr int maxAge = 45;

    struct Fire
    {
        QcBlockPos pos;
        // Where the fire that lit it began (the spark); fire spreads only within a reach of it.
        QcBlockPos origin;
        // Steps taken; a new fire starts with the age of the fire that lit it.
        quint8 age = 0;
        quint64 due = 0;
        // Whether it stands on eternal ground: it keeps no place in the caps. The queue keeps it in
        // step with the block below (noteBlock, restore); add() takes it from the caller.
        bool eternal = false;
    };

    // A fire as a chunk keeps it while it is not loaded: ticks left to its next step instead of the
    // tick it was due.
    struct Saved
    {
        QcBlockPos pos;
        QcBlockPos origin;
        quint8 age = 0;
        quint32 ticksLeft = 0;

        bool operator==(const Saved &) const = default;
    };

    struct Limits
    {
        qsizetype perChunk = QcServerLimits{}.firesPerChunk;
        qsizetype total = QcServerLimits{}.fires;
    };

    QcFireQueue() = default;
    // Both indexed by block state id: which states are fire, and on which fire burns for ever.
    QcFireQueue(std::vector<bool> fire, std::vector<bool> eternalGround);

    bool isFire(QcBlockId block) const { return std::size_t(block) < m_fire.size() && m_fire[std::size_t(block)]; }
    bool isEternalGround(QcBlockId block) const
    {
        return std::size_t(block) < m_eternalGround.size() && m_eternalGround[std::size_t(block)];
    }
    void setLimits(Limits limits) { m_limits = limits; }
    const Limits &limits() const { return m_limits; }

    // A block was set (QcServerWorld::setBlock). When the ground of a fire turns eternal, the fire
    // leaves the caps; when it stops being eternal, the fire takes a place again or, without room,
    // waits in takeRefused() to be put out.
    void noteBlock(QcBlockPos pos, QcBlockId before, QcBlockId block);
    // A chunk entered the world: its fire blocks without an entry wait for adoption. Only the
    // sections in `sections` (bit n: section n, QcSectionMarks) are scanned.
    void noteChunk(QcChunkPos pos, const QcChunk &chunk, quint32 sections);
    // Drops the entries and waiting fires of a chunk that left the world or was generated again.
    void forgetChunk(QcChunkPos pos);

    // Whether one more fire fits into the chunk and the world (fires on eternal ground always fit).
    bool hasRoom(QcChunkPos chunk) const;
    // Plans the next step of a fire at `fire.due`; false when the position has an entry already.
    bool add(const Fire &fire);
    // Drops the entry of a position whose block is no fire any more.
    void drop(QcBlockPos pos);
    bool contains(QcBlockPos pos) const { return m_fires.contains(pos); }
    std::optional<Fire> entry(QcBlockPos pos) const;
    // Takes the fire due first if it is due at `now` out of the order of steps; it keeps its entry
    // (and counts for the caps) until its block goes away or plan() gives it its next step.
    std::optional<Fire> takeDue(quint64 now);
    // Plans the next step of a fire that has an entry at `fire.due`, with its new age; false without
    // one. The entry keeps its own `eternal`.
    bool plan(const Fire &fire);
    bool hasDue(quint64 now) const { return !m_due.empty() && m_due.begin()->first.first <= now; }
    qsizetype dueCount(quint64 now) const;
    // Fire blocks set without an entry since the last call, sorted.
    QList<QcBlockPos> takeUnplanned();
    // Fire blocks that lost their entry for want of room (a restored fire, a fire whose eternal
    // ground went away) since the last call, sorted: QcServerFire puts them out. They are never
    // adopted as new sparks, so a fire cannot come back with a new budget.
    QList<QcBlockPos> takeRefused();
    qsizetype refusedCount() const { return m_refused.size(); }

    // The fires of a chunk as it keeps them, in the order of their steps; taken out of the queue or
    // left in it (a save).
    QList<Saved> takeChunk(QcChunkPos chunk, quint64 now);
    QList<Saved> savedOf(QcChunkPos chunk, quint64 now) const;
    // Puts back the fires of a chunk that entered the world, their steps counted from `now`. Fires
    // whose block is no fire any more are left out; those that find no room wait in takeRefused(); a
    // fire block without an entry waits for adoption.
    void restore(const QList<Saved> &fires, quint64 now, const QcChunk &chunk);

    // Every fire with an entry, eternal ones included.
    qsizetype size() const { return m_fires.size(); }
    // The fires that count toward the caps: all but those on eternal ground.
    qsizetype counted() const { return m_fires.size() - m_eternal; }
    qsizetype countIn(QcChunkPos chunk) const { return m_perChunk.value(chunk); }
    qsizetype unplannedCount() const { return m_unplanned.size(); }
    // Every fire, sorted by position (tests, checks).
    QList<Fire> fires() const;

private:
    // Due tick and order of planning.
    using Key = std::pair<quint64, quint64>;
    // The key of a fire taking its step: in no order of steps.
    static constexpr Key unplanned{~quint64(0), ~quint64(0)};

    struct Entry
    {
        Fire fire;
        Key key;
    };

    void remove(QcBlockPos pos);
    // The ground below a fire was set: `eternal` is what it is now.
    void noteGround(QcBlockPos fire, bool eternal);
    // Counts a fire toward the caps (`by` 1) or takes it out of them (-1).
    void count(QcBlockPos pos, int by);
    QList<Entry> entriesOf(QcChunkPos chunk) const;

    std::vector<bool> m_fire;
    std::vector<bool> m_eternalGround;
    Limits m_limits;
    QHash<QcBlockPos, Entry> m_fires;
    std::map<Key, QcBlockPos> m_due;
    // The positions of the entries of each chunk, so that a chunk's fires are found without
    // looking at every fire.
    QHash<QcChunkPos, QSet<QcBlockPos>> m_byChunk;
    // Fires counted toward the caps per chunk (all but eternal ones), and the eternal ones.
    QHash<QcChunkPos, qsizetype> m_perChunk;
    qsizetype m_eternal = 0;
    QSet<QcBlockPos> m_unplanned;
    QSet<QcBlockPos> m_refused;
    quint64 m_order = 0;
};
