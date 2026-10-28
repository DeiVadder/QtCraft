#pragma once

#include "server/entities/QcCreature.h"

#include <QHash>
#include <QList>
#include <QSet>

#include <memory>
#include <optional>

class QcContentRegistry;
class QcPathCellTable;
class QcServerWorld;
class QcVillages;
struct QcVillage;
struct QcVillageGolemSettings;

// Villagers summon iron golems like in the reference game (decision E-470), checked every few
// seconds by QcVillagePopulation. A villager wants a golem while it slept in its bed within
// `sleptSeconds` (QcVillager::sleptAt). Villagers of one village (or, without a village, any) that
// want one are grouped; one of them summons a golem when no golem of the kind stands within
// `golemDistance` of it and either it panicked at a hostile creature lately (QcVillager::panickedAt)
// with at least `minVillagers` wanting ones within `gatherDistance` (itself included), or at least
// `gossipVillagers` wanting ones are there (gossip, with `gossipChance` per check). The golem
// appears in the box of `spawnRadius` around it (vanilla's 16 × 13 × 16), at the highest place of a
// drawn column where it stands on dry, opaque ground that does not hurt (not on glass, panes, leaves
// or ice) with its whole body clear. A village summons at most once per `cooldownSeconds`
// (QcVillage::lastGolem); villagers without a village keep that pause per chunk (not saved), so
// several farms without a village may summon in one check. All golems stay below the world's limit
// (QcServerLimits villageGolems, counted per golem kind: those every village counted last in its
// area and those loaded outside villages, built ones included; golems in unloaded chunks outside
// villages are not counted). The golem is the creature kind of the village style's
// QcVillageGolemSettings::mob (decision E-421); while the content has no such kind the rule rests
// (logged once). A golem belongs to the village whose area it stands in; nothing is claimed
// (decision E-415).
class QcVillageGolems
{
public:
    // Columns tried for a golem's place around the summoning villager.
    static constexpr int placeAttempts = 10;

    struct Spawn
    {
        // The summoning villagers' village (0: none) and the golem's kind and feet.
        qint64 village = 0;
        int type = 0;
        QcVec3 position;
    };

    struct Context
    {
        QcServerWorld &world;
        const QcPathCellTable &cells;
        double worldTime = 0.0;
        quint64 seed = 0;
        // Golems in the world at most.
        qint64 worldLimit = 0;
    };

    // Why a villager wants a golem now (one without a mind, QcCreature::noAi, wants none): it slept lately (Gossip), and panicked lately too (Panic).
    enum class Want : quint8 { None, Gossip, Panic };

    explicit QcVillageGolems(std::shared_ptr<const QcContentRegistry> content);

    // The golems' creature kind in villages of `style` (QcVillageCatalog::golemOfStyle; the empty
    // style: the settings' own); none while the content lacks it (logged the first time) or the
    // style has no golems.
    std::optional<int> kind(const QString &style) const;
    // Counts the golems of the loaded villages among `villages` (those whose claims were
    // reconciled; villagers of other villages summon none) and returns the golems villagers summon
    // now; the summoning villages' lastGolem is set.
    QList<Spawn> check(const QList<QcCreature> &mobs, const QSet<qint64> &villages, const Context &context);
    // Whether `villager` wants a golem at `worldTime`, and why.
    static Want wants(const QcCreature &villager, const QcVillageGolemSettings &settings, double worldTime);
    // Golems of kind `type` in the area of `village`.
    static int golemsIn(const QcVillage &village, const QList<QcCreature> &mobs, int type);
    // A place around `around` where a golem of `type` stands: columns drawn (by `draw`) within
    // `radius`, each tried from spawnHeight above `around` down to spawnHeight below it.
    std::optional<QcVec3> placeNear(const QcVec3 &around, int type, int radius, const Context &context, quint64 draw) const;
    // Golems of kind `type` in the world: outside villages those loaded, in each village those it
    // counted last or those loaded in its area, whichever is more (QcVillages::golems).
    qint64 golemsInWorld(const QList<QcCreature> &mobs, int type, const QcVillages &villages) const;

private:
    // The villagers of one village (0: without one) that want a golem, by id.
    struct Group
    {
        qint64 village = 0;
        QList<const QcCreature *> wanting;
    };
    // The villagers that want a golem now, grouped by village in the order of the ids (0: without
    // one); of the villages only those among `villages`.
    QList<Group> groups(const QList<QcCreature> &mobs, const QSet<qint64> &villages, const Context &context) const;
    // The golems summoned by `group` now, at most `allowed`: of a village the first villager's that
    // calls (calls()) with no golem near it; without a village every such villager's whose chunk
    // rested (several farms).
    QList<Spawn> summon(const Group &group, int type, const QList<QcCreature> &mobs, const Context &context, qint64 allowed);
    // Whether `summoner` of `group` calls a golem now: on the panic path or (by `roll`) the gossip
    // path.
    static bool calls(const QcCreature &summoner, const Group &group, const QcVillageGolemSettings &settings, double worldTime, double roll);
    // Forgets the pauses of chunks without a village that ended (or lie in the future).
    void forgetRestedChunks(double worldTime);
    // Whether `village` (or, without one, the chunk of `at` and those next to it) rested
    // cooldownSeconds since its last golem.
    bool rested(const QcVillage *village, const QcVillageGolemSettings &settings, const QcVec3 &at, double worldTime) const;
    // Every chunk of the village's area and reach is loaded: its golems are all in the world.
    static bool loaded(const QcServerWorld &world, const QcVillage &village);
    // Every chunk within `distance` of `at` is loaded.
    static bool loadedAround(const QcServerWorld &world, const QcVec3 &at, double distance);
    // Counts the golems of a loaded village; a last golem after the world time (it went back) is
    // forgotten.
    static void recount(QcVillage &village, const QList<QcCreature> &mobs, int type, const Context &context);
    // The feet of a golem of `type` in `feet`: on dry, opaque ground that does not hurt, the whole
    // body clear of blocks.
    std::optional<QcVec3> standsAt(QcBlockPos feet, int type, const Context &context) const;
    // Whether the column's floor (a block in the cell with collision, else the one below) is of the
    // opaque render layer.
    bool opaqueFloor(QcBlockPos column, const Context &context) const;

    std::shared_ptr<const QcContentRegistry> m_content;
    mutable bool m_missingLogged = false;
    // When villagers without a village last summoned a golem, by the chunk of the summoner.
    QHash<QcChunkPos, double> m_lastWithoutVillage;
};
