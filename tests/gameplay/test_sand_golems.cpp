#include "common/diagnostics/QcThreadCpuClock.h"
#include "common/physics/QcPhysics.h"
#include "QcServerTestFixture.h"
#include "QcTestUtil.h"
#include "server/explosions/QcServerExplosions.h"
#include "server/guardians/QcCrumbling.h"
#include "server/guardians/QcGuardians.h"

#include <QDeadlineTimer>
#include <QMutex>
#include <QTemporaryDir>
#include <QThread>

#include <atomic>

using namespace Qt::StringLiterals;

namespace {

// The guardian zones of the test terrain. Generators are called from generation threads while the
// test changes the zones on the main thread, so the list itself is never changed: every change swaps
// in a new immutable list under the mutex, and readers keep the snapshot they took.
class GuardSiteList
{
public:
    std::shared_ptr<const QList<QcGuardSite>> snapshot() const
    {
        const QMutexLocker lock(&m_mutex);
        return m_sites;
    }

    QcGuardSite front() const { return snapshot()->front(); }

    // Applies `change` to a copy of the zones and publishes the copy.
    template<typename Change>
    void edit(Change change)
    {
        const QMutexLocker lock(&m_mutex);
        auto changed = std::make_shared<QList<QcGuardSite>>(*m_sites);
        change(*changed);
        m_sites = std::move(changed);
    }

    void append(const QcGuardSite &site)
    {
        edit([&site](QList<QcGuardSite> &sites) { sites.append(site); });
    }

    void clear()
    {
        const QMutexLocker lock(&m_mutex);
        m_sites = std::make_shared<const QList<QcGuardSite>>();
    }

private:
    mutable QMutex m_mutex;
    std::shared_ptr<const QList<QcGuardSite>> m_sites = std::make_shared<const QList<QcGuardSite>>();
};

// The test terrain with guardian zones the test sets after joining (QcWorldGenerator::structureSitesNear).
class GuardedTerrain : public QcTestTerrainGenerator
{
public:
    GuardedTerrain(std::shared_ptr<const QcContentRegistry> content, std::shared_ptr<const GuardSiteList> sites)
        : QcTestTerrainGenerator(std::move(content))
        , m_sites(std::move(sites))
    {
    }

    // Cold: the zones count as planned only once structureSitesNear ran (QcWorldGenerator::plannedStructureSitesNear).
    void makeCold()
    {
        m_planned = false;
        m_plannedOnMainThread = false;
        m_cold = true;
    }
    bool plannedOnMainThread() const { return m_plannedOnMainThread; }
    bool planned() const { return m_planned; }

    std::optional<QcStructureSites> plannedStructureSitesNear(QcBlockPos from, int reach) const override
    {
        if (m_cold && !m_planned) {
            return std::nullopt;
        }
        return QcStructureSites{sitesNear(from, reach), {}};
    }

    QcStructureSites structureSitesNear(QcBlockPos from, int reach) const override
    {
        m_plannedOnMainThread = m_plannedOnMainThread || QThread::isMainThread();
        m_planned = true;
        return QcStructureSites{sitesNear(from, reach), {}};
    }

private:
    QList<QcGuardSite> sitesNear(QcBlockPos from, int reach) const
    {
        const std::shared_ptr<const QList<QcGuardSite>> sites = m_sites->snapshot();
        QList<QcGuardSite> near;
        for (const QcGuardSite &site : *sites) {
            if (std::abs(site.min.x - from.x) <= reach + 16 && std::abs(site.min.z - from.z) <= reach + 16) {
                near.append(site);
            }
        }
        return near;
    }

    std::shared_ptr<const GuardSiteList> m_sites;
    std::atomic<bool> m_cold{false};
    mutable std::atomic<bool> m_planned{false};
    mutable std::atomic<bool> m_plannedOnMainThread{false};
};

double threadSeconds()
{
    return static_cast<double>(QcThreadCpuClock::nowNs()) / 1e9;
}

} // namespace

// Sand golems (decisions E-591..E-594): a guardian zone gets its golem back under its rule only
// (fewer than `max`, cooldown, half of it for a zone seen first, no player close, one within reach,
// dark, loaded; zones planned off the tick thread), the golem keeps to
// its zone and chases only players near it, and it crumbles into its build where it touches a water
// source, not flowing water; a farm yields at most one golem per cooldown; a name tag frees a golem
// from its zone. Ticks are stepped by hand.
class TestSandGolems : public QcServerTestFixture
{
    Q_OBJECT

private:
    static constexpr double defaultCooldown = 6.0;
    std::shared_ptr<GuardSiteList> m_sites = std::make_shared<GuardSiteList>();
    // The terrain of the joined world (owned by the server).
    GuardedTerrain *m_terrain = nullptr;
    // The cellar: a closed stone room east of the player, its floor zone 5 x 5.
    QcBlockPos m_cellar;

    int golemType() const { return *m_content->mobIndex(u"qtcraft:sand_golem"_s); }
    QcServerClient &player() { return *m_server->entities().clients().front(); }
    const QcGuardians &guardians() { return m_server->creatures().guardians(); }

    QList<const QcCreature *> golems() const
    {
        QList<const QcCreature *> found;
        for (const QcCreature &mob : m_server->mobs().mobs()) {
            if (mob.type == golemType()) {
                found.append(&mob);
            }
        }
        return found;
    }

    void teleport(const QcVec3 &to)
    {
        chat(u"/tp %1 %2 %3"_s.arg(to.x).arg(to.y).arg(to.z));
        ticks(1);
    }

    // Ticks within which a zone seen for the first time gets its guardian: half its cooldown and two
    // checks.
    static int firstWait(double cooldown = defaultCooldown) { return static_cast<int>(cooldown * 10.0) + 2 * QcGuardians::checkTicks; }

    QcGuardSite site(QcBlockPos min, QcBlockPos max, double cooldown = defaultCooldown) const
    {
        QcGuardSite site;
        site.id = qHashMulti(7u, min.x, min.y, min.z);
        site.min = min;
        site.max = max;
        site.rule.mob = u"qtcraft:sand_golem"_s;
        site.rule.cooldownSeconds = cooldown;
        return site;
    }

    // A flat stone ground around the player, open air above, no creatures, in creative at night.
    void joinWorld(QcServerConfig config = {})
    {
        m_sites->clear();
        auto terrain = std::make_unique<GuardedTerrain>(m_content, m_sites);
        m_terrain = terrain.get();
        join(std::move(config), std::move(terrain));
        chat(u"/gamemode creative"_s);
        chat(u"/gamerule mobSpawning false"_s);
        chat(u"/gamerule daylightCycle false"_s);
        chat(u"/time set 6000"_s);
        loadAround(m_feet, 3);
        for (int dx = -12; dx <= 40; ++dx) {
            for (int dz = -12; dz <= 12; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:stone"));
                for (int dy = 0; dy <= 8; ++dy) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
        m_server->mobs().removeAll();
        m_cellar = m_feet.offset(24, 0, 0);
    }

    // The cellar 7 x 7 inside, 4 high, walled and roofed with stone; its zone the middle 5 x 5.
    void buildCellar()
    {
        for (int dx = -4; dx <= 4; ++dx) {
            for (int dz = -4; dz <= 4; ++dz) {
                for (int dy = 0; dy <= 4; ++dy) {
                    const bool wall = std::abs(dx) == 4 || std::abs(dz) == 4 || dy == 4;
                    m_server->world().setBlock(m_cellar.offset(dx, dy, dz), wall ? id("qtcraft:stone") : qcAirBlockId);
                }
            }
        }
        m_sites->append(site(m_cellar.offset(-2, 0, -2), m_cellar.offset(2, 0, 2)));
    }

    // Ticks until a golem guards the cellar, at most `limit`; the ticks it took, or -1.
    int ticksUntilGolem(int limit)
    {
        for (int tick = 1; tick <= limit; ++tick) {
            ticks(1);
            if (!golems().isEmpty()) {
                return tick;
            }
        }
        return -1;
    }

    // A golem standing at the cell's middle (its feet in `feet`), facing `yaw`.
    quint32 golemAt(QcBlockPos feet, float yaw = 0.0f)
    {
        const quint32 id = m_server->mobs().spawn(golemType(), {feet.x + 0.5, static_cast<double>(feet.y), feet.z + 0.5}, m_server->entities().newId());
        m_server->mobs().find(id)->yaw = yaw;
        return id;
    }

    QString nameAt(QcBlockPos pos) const { return m_content->block(*m_server->world().blockAt(pos)).name; }

    // A player digging out the crumbled builds on the farm: sand and heads on the pad's level and
    // above give way to air. The farm's water never gets sand (buildFarm), so nothing is refilled.
    void clearBuild(QcBlockPos pad)
    {
        for (int cell = 0; cell < 4 * 5 * 5; ++cell) {
            const QcBlockPos at = pad.offset(cell % 5 - 2, cell / 25, cell / 5 % 5 - 2);
            const QString name = nameAt(at);
            if (name == u"qtcraft:sand"_s || name == u"qtcraft:carved_sandstone"_s) {
                m_server->world().setBlock(at, qcAirBlockId);
            }
        }
        QcTestUtil::pump();
    }

    // The items of `name` lying in the world or about to be dropped.
    int dropped(const char *name) const
    {
        const QcItemId wanted = item(name);
        int total = 0;
        for (const QcItemEntity &item : m_server->items().items()) {
            total += item.stack.item == wanted ? item.stack.count : 0;
        }
        for (const auto &drop : m_server->items().queued()) {
            total += drop.stack.item == wanted ? drop.stack.count : 0;
        }
        return total;
    }

    // The water sources in the 5 x 5 cells under the pad's level around `pad`.
    int poolSources(QcBlockPos pad) const
    {
        int sources = 0;
        for (int dx = -2; dx <= 2; ++dx) {
            for (int dz = -2; dz <= 2; ++dz) {
                sources += m_server->world().blockAt(pad.offset(dx, -1, dz)) == id("qtcraft:water") ? 1 : 0;
            }
        }
        return sources;
    }

    // A working farm: the pad on a stone floor with water sources only under its four diagonal
    // neighbours, which the golem's body touches; its arms (beside the middle, across its facing)
    // fall onto the floor next to its feet, never into the water.
    void buildFarm(QcBlockPos pad)
    {
        for (int dx = -2; dx <= 2; ++dx) {
            for (int dz = -2; dz <= 2; ++dz) {
                const bool diagonal = std::abs(dx) == 1 && std::abs(dz) == 1;
                m_server->world().setBlock(pad.offset(dx, -1, dz), diagonal ? id("qtcraft:water") : id("qtcraft:stone"));
                m_server->world().setBlock(pad.offset(dx, -2, dz), id("qtcraft:stone"));
            }
        }
    }

    // A pillar in a pool of 24 water sources; its top is `pad`.
    void buildPool(QcBlockPos pad)
    {
        for (int dx = -2; dx <= 2; ++dx) {
            for (int dz = -2; dz <= 2; ++dz) {
                m_server->world().setBlock(pad.offset(dx, -1, dz), dx == 0 && dz == 0 ? id("qtcraft:stone") : id("qtcraft:water"));
                m_server->world().setBlock(pad.offset(dx, -2, dz), id("qtcraft:stone"));
            }
        }
    }

private slots:
    void aGolemComesIntoAnEmptyCellarOnlyOnce()
    {
        joinWorld();
        buildCellar();
        // Seen for the first time, the zone waits half its cooldown.
        const int waited = ticksUntilGolem(firstWait());
        QVERIFY2(waited >= int(defaultCooldown * 10.0), qPrintable(QString::number(waited)));
        const QcCreature &golem = *golems().constFirst();
        QVERIFY(golem.guards);
        QCOMPARE(golem.guards->site, m_sites->front().id);
        QVERIFY(m_sites->front().contains(QcPhysics::blockContaining(golem.motion.position)));
        QVERIFY(!golem.persistent);

        // The zone holds one: no second one, however long it waits.
        ticks(QcGuardians::checkTicks * 20);
        QCOMPARE(golems().size(), 1);
        QCOMPARE(guardians().guardianCount(m_sites->front().id), 1);

        // Never kept with the world (its post is not saved): /mob noai leaves it a guardian of the zone.
        QcCreature &guardian = *m_server->mobs().find(golems().constFirst()->id);
        chat(u"/mob %1 noai on"_s.arg(guardian.id));
        QVERIFY(guardian.noAi);
        QVERIFY(!guardian.persistent);
        QVERIFY(guardian.guards);
    }

    // The tick thread never plans: zones in regions not planned yet are planned on a generation
    // thread, and the golem comes with a later check.
    void zonesArePlannedOffTheTickThread()
    {
        joinWorld();
        buildCellar();
        m_terrain->makeCold();
        // The planning job runs beside the ticks: ticked until it is done, however loaded the machine.
        QDeadlineTimer deadline(30000);
        while (!m_terrain->planned() && !deadline.hasExpired()) {
            ticks(1);
        }
        QVERIFY(ticksUntilGolem(firstWait()) > 0);
        QVERIFY(m_terrain->planned());
        QVERIFY(!m_terrain->plannedOnMainThread());
    }

    void playersMustBeNeitherTooCloseNorTooFar()
    {
        joinWorld();
        buildCellar();
        // 7 blocks from the zone: too close.
        teleport({m_cellar.x - 9.5, static_cast<double>(m_feet.y), m_cellar.z + 0.5});
        QCOMPARE(ticksUntilGolem(firstWait()), -1);
        // 60 blocks away: too far.
        teleport({m_cellar.x - 62.5, static_cast<double>(m_feet.y), m_cellar.z + 0.5});
        QCOMPARE(ticksUntilGolem(QcGuardians::checkTicks * 3), -1);
        // In between it comes.
        teleport({m_cellar.x - 24.5, static_cast<double>(m_feet.y), m_cellar.z + 0.5});
        QVERIFY(ticksUntilGolem(QcGuardians::checkTicks * 2) > 0);
    }

    void aLitCellarStaysEmpty()
    {
        joinWorld();
        buildCellar();
        // Torches on the walls light every cell of the zone above 7.
        for (const auto &[dx, dz] : {std::pair{0, -3}, {0, 3}, {-3, 0}, {3, 0}}) {
            m_server->world().setBlock(m_cellar.offset(dx, 1, dz), id("qtcraft:torch"));
        }
        ticks(1);
        QVERIFY(m_server->world().blockLight(m_cellar.offset(2, 0, 2)) > 7);
        QCOMPARE(ticksUntilGolem(firstWait() + QcGuardians::checkTicks * 2), -1);

        for (const auto &[dx, dz] : {std::pair{0, -3}, {0, 3}, {-3, 0}, {3, 0}}) {
            m_server->world().setBlock(m_cellar.offset(dx, 1, dz), qcAirBlockId);
        }
        QVERIFY(ticksUntilGolem(QcGuardians::checkTicks * 2) > 0);
    }

    void theCooldownRunsFromTheLastFullZone()
    {
        joinWorld();
        buildCellar();
        m_sites->edit([](QList<QcGuardSite> &sites) { sites.front().rule.cooldownSeconds = 30.0; });
        QVERIFY(ticksUntilGolem(firstWait(30.0)) > 0);
        ticks(QcGuardians::checkTicks * 3);

        // Gone (killed, crumbled): the next one comes a cooldown after the zone was last seen full.
        QVERIFY(m_server->mobs().remove(golems().constFirst()->id));
        const int waited = ticksUntilGolem(40 * 20);
        const int cooldown = 30 * 20;
        QVERIFY2(waited >= cooldown - QcGuardians::checkTicks, qPrintable(QString::number(waited)));
        QVERIFY2(waited <= cooldown + QcGuardians::checkTicks, qPrintable(QString::number(waited)));
    }

    void theWorldLimitHoldsZonesBack()
    {
        QcServerConfig config;
        config.limits.guardians = 0;
        m_sites->clear();
        join(config, std::make_unique<GuardedTerrain>(m_content, m_sites));
        chat(u"/gamemode creative"_s);
        m_server->mobs().removeAll();
        m_cellar = m_feet.offset(24, 0, 0);
        loadAround(m_cellar, 1);
        buildCellar();
        QCOMPARE(ticksUntilGolem(firstWait() + QcGuardians::checkTicks * 2), -1);
    }

    void itCrumblesAtAWaterSourceIntoItsBuild()
    {
        joinWorld();
        const QcBlockPos feet = m_feet.offset(6, 0, 0);
        golemAt(feet, 0.0f);
        // Flowing water beside it does nothing.
        m_server->world().setBlock(feet.offset(-1, 0, 0), m_content->withProperty(id("qtcraft:water"), u"level"_s, u"3"_s));
        ticks(1);
        QCOMPARE(golems().size(), 1);
        m_server->world().setBlock(feet.offset(-1, 0, 0), qcAirBlockId);
        m_server->mobs().find(golems().constFirst()->id)->motion.position = {feet.x + 0.5, static_cast<double>(feet.y), feet.z + 0.5};

        // A source block beside its body: gone, its build in its place (arms east and west of a
        // golem facing north, the head facing north too).
        const int before = m_server->creatures().crumbling().crumbled();
        m_server->world().setBlock(feet.offset(0, 0, 1), id("qtcraft:water"));
        ticks(1);
        QVERIFY(golems().isEmpty());
        QCOMPARE(m_server->creatures().crumbling().crumbled(), before + 1);
        QCOMPARE(nameAt(feet), u"qtcraft:sand"_s);
        QCOMPARE(nameAt(feet.offset(0, 1, 0)), u"qtcraft:sand"_s);
        QCOMPARE(nameAt(feet.offset(0, 2, 0)), u"qtcraft:carved_sandstone"_s);
        QCOMPARE(m_content->property(*m_server->world().blockAt(feet.offset(0, 2, 0)), u"facing"_s), u"north"_s);
        // The arms fall to the ground beside the feet.
        ticks(40);
        QCOMPARE(nameAt(feet.offset(-1, 0, 0)), u"qtcraft:sand"_s);
        QCOMPARE(nameAt(feet.offset(1, 0, 0)), u"qtcraft:sand"_s);
        QCOMPARE(m_server->items().items().size() + m_server->items().queued().size(), qsizetype(0));
    }

    void aBlastHurtsItWithoutCrumblingIt()
    {
        // An explosion (decision E-602) hurts a golem like any creature that sees it; only a water
        // source crumbles it (decision E-593), so it stays a golem.
        joinWorld();
        const QcBlockPos feet = m_feet.offset(6, 0, 0);
        const quint32 golem = golemAt(feet, 0.0f);
        const double full = m_server->mobs().find(golem)->health;
        const int before = m_server->creatures().crumbling().crumbled();
        const QcBlockPos centre = feet.offset(0, 0, 3);
        m_server->systems().explosions.explode({QcVec3{centre.x + 0.5, centre.y + 0.5, centre.z + 0.5}, 4.0, true, false});
        ticks(2);
        QCOMPARE(m_server->creatures().crumbling().crumbled(), before);
        QCOMPARE(golems().size(), 1);
        QVERIFY2(golems().constFirst()->health < full, qPrintable(u"health %1 of %2"_s.arg(golems().constFirst()->health).arg(full)));
    }

    void aBuildWithoutRoomDropsItsBlocks()
    {
        joinWorld();
        const QcBlockPos feet = m_feet.offset(6, 0, 0);
        // A golem facing east under a low roof: the head's cell is stone, so the head drops.
        golemAt(feet, 90.0f);
        m_server->world().setBlock(feet.offset(0, 2, 0), id("qtcraft:stone"));
        m_server->world().setBlock(feet.offset(-1, 0, 0), id("qtcraft:water"));
        ticks(2);
        QVERIFY(golems().isEmpty());
        QCOMPARE(nameAt(feet.offset(0, 1, 0)), u"qtcraft:sand"_s);
        QCOMPARE(nameAt(feet.offset(0, 2, 0)), u"qtcraft:stone"_s);
        // Its arms lie north and south of its middle and fall to the ground; the water source stays.
        ticks(40);
        QCOMPARE(nameAt(feet.offset(0, 0, -1)), u"qtcraft:sand"_s);
        QCOMPARE(nameAt(feet.offset(0, 0, 1)), u"qtcraft:sand"_s);
        QCOMPARE(nameAt(feet.offset(-1, 0, 0)), u"qtcraft:water"_s);
        bool head = false;
        for (const QcItemEntity &item : m_server->items().items()) {
            head = head || item.stack.item == this->item("qtcraft:carved_sandstone");
        }
        for (const auto &drop : m_server->items().queued()) {
            head = head || drop.stack.item == this->item("qtcraft:carved_sandstone");
        }
        QVERIFY(head);
    }

    // Decision E-593: falling sand may fill a water hole; a farm must keep its water out of the arms'
    // way (buildFarm).
    void armsCanFillTheWaterBesideThePillar()
    {
        joinWorld();
        const QcBlockPos pad = m_feet.offset(20, 0, 0);
        buildPool(pad);
        golemAt(pad, 0.0f);
        ticks(2);
        QVERIFY(golems().isEmpty());

        // Feet, middle and head stand on the pillar; the arms east and west fall into the pool.
        ticks(40);
        QCOMPARE(nameAt(pad), u"qtcraft:sand"_s);
        QCOMPARE(nameAt(pad.offset(0, 1, 0)), u"qtcraft:sand"_s);
        QCOMPARE(nameAt(pad.offset(0, 2, 0)), u"qtcraft:carved_sandstone"_s);
        QCOMPARE(nameAt(pad.offset(-1, -1, 0)), u"qtcraft:sand"_s);
        QCOMPARE(nameAt(pad.offset(1, -1, 0)), u"qtcraft:sand"_s);
        QCOMPARE(poolSources(pad), 22);
        QCOMPARE(dropped("qtcraft:sand"), 0);
    }

    void aGolemThatMayNotChangeTheWorldLeavesItems_data()
    {
        QTest::addColumn<QString>("why");
        QTest::newRow("mobGriefing off") << u"rule"_s;
        QTest::newRow("confined") << u"confined"_s;
        QTest::newRow("no mind") << u"noAi"_s;
    }

    // QcCreature::mayGrief (decision E-510): its build drops as items, no block is set; sand still comes.
    void aGolemThatMayNotChangeTheWorldLeavesItems()
    {
        QFETCH(QString, why);
        joinWorld();
        const QcBlockPos feet = m_feet.offset(6, 0, 0);
        QcCreature &golem = *m_server->mobs().find(golemAt(feet, 0.0f));
        golem.confined = why == u"confined"_s;
        golem.noAi = why == u"noAi"_s;
        if (why == u"rule"_s) {
            chat(u"/gamerule mobGriefing false"_s);
        }
        m_server->world().setBlock(feet.offset(-1, 0, 0), id("qtcraft:water"));
        ticks(2);
        QVERIFY(golems().isEmpty());
        const QList<QcBlockPos> build = QcCrumbling::buildCells(feet, 0.0);
        for (const QcBlockPos &cell : build) {
            QCOMPARE(m_server->world().blockAt(cell), std::optional<QcBlockId>(qcAirBlockId));
        }
        QCOMPARE(dropped("qtcraft:sand"), 4);
        QCOMPARE(dropped("qtcraft:carved_sandstone"), 1);
        QCOMPARE(nameAt(feet.offset(-1, 0, 0)), u"qtcraft:water"_s);
    }

    void itKeepsToItsZoneAndChasesOnlyPlayersNearIt()
    {
        joinWorld();
        chat(u"/gamemode survival"_s);
        chat(u"/time set 18000"_s);
        // An open zone on the stone ground, 3 x 3, east of the player.
        const QcBlockPos zone = m_feet.offset(16, 0, 0);
        m_sites->append(site(zone.offset(-1, 0, -1), zone.offset(1, 0, 1)));
        const quint32 id = golemAt(zone.offset(9, 0, 0));
        m_server->mobs().find(id)->guards = QcGuardPost{m_sites->front().id, m_sites->front().min, m_sites->front().max};

        // Outside its zone, it walks back into it (the player 15 blocks from the zone: no target).
        bool home = false;
        for (int tick = 0; tick < 600 && !home; ++tick) {
            ticks(1);
            home = m_sites->front().contains(QcPhysics::blockContaining(m_server->mobs().find(id)->motion.position));
        }
        QVERIFY(home);
        QCOMPARE(m_server->mobs().find(id)->chasing, 0U);

        // A player within its leash (6) of the zone is chased, one beyond it is let go.
        teleport({zone.x - 5.5, static_cast<double>(m_feet.y), zone.z + 0.5});
        bool chased = false;
        for (int tick = 0; tick < 60 && !chased; ++tick) {
            ticks(1);
            chased = m_server->mobs().find(id) && m_server->mobs().find(id)->chasing == player().id;
        }
        QVERIFY(chased);
        teleport({zone.x - 14.5, static_cast<double>(m_feet.y), zone.z + 0.5});
        ticks(30);
        QCOMPARE(m_server->mobs().find(id)->chasing, 0U);
    }

    // A name tag frees a golem from its zone (decision E-873): it is an ordinary kept creature, saved
    // with the world, and the zone brings a successor after its cooldown.
    void aNameTagFreesTheGolemFromItsZone()
    {
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        config.autosaveSeconds = 0;
        joinWorld(config);
        buildCellar();
        QVERIFY(ticksUntilGolem(firstWait()) > 0);
        const quint32 first = golems().constFirst()->id;
        m_server->mobs().find(first)->noAi = true;
        const QcVec3 at = m_server->mobs().find(first)->motion.position;
        teleport({at.x - 1.5, at.y, at.z});
        player().inventory.setSlot(player().inventory.selectedHotbarSlot(), QcItemStack{item("qtcraft:name_tag"), 1, 0}.withName(u"Sandy"_s));
        send(QcMsg::UseEntity{first});
        const QcCreature &named = *m_server->mobs().find(first);
        QCOMPARE(named.name, u"Sandy"_s);
        QVERIFY(!named.guards);
        QVERIFY(named.persistent);
        QCOMPARE(guardians().guardianCount(m_sites->front().id), 0);

        // Back out of the cellar, the zone gets a successor.
        teleport({m_feet.x + 0.5, static_cast<double>(m_feet.y), m_feet.z + 0.5});
        bool successor = false;
        for (int tick = 0; tick < 40 * QcGuardians::checkTicks && !successor; ++tick) {
            ticks(1);
            successor = golems().size() == 2;
        }
        QVERIFY(successor);
        QCOMPARE(guardians().guardianCount(m_sites->front().id), 1);

        // Saved and loaded: the named one comes back without a post, the guardian does not (its zone
        // brings it again).
        QVERIFY(m_server->stop().has_value());
        m_client.reset();
        auto terrain = std::make_unique<GuardedTerrain>(m_content, m_sites);
        m_terrain = terrain.get();
        m_server = std::make_unique<QcServer>(config, m_content, std::move(terrain));
        QVERIFY(m_server->openWorld().has_value());
        loadAround(m_cellar);
        QCOMPARE(golems().size(), 1);
        QCOMPARE(golems().constFirst()->name, u"Sandy"_s);
        QVERIFY(!golems().constFirst()->guards);
        QVERIFY(golems().constFirst()->persistent);
    }

    // A sand farm: every golem appears beside water and crumbles; the build is cleared at once (a
    // player digging it out). In one hour of game time the zone gives at most one golem per
    // cooldown, measured; the tick cost of the zone and the crumbling is measured too.
    void farmYieldIsBoundedPerHour()
    {
        joinWorld();
        // A farm that keeps its water, the zone its pad.
        const QcBlockPos pad = m_feet.offset(20, 0, 0);
        buildFarm(pad);
        m_sites->append(site(pad, pad, 120.0));
        const int hour = 3600 * 20;
        const int before = m_server->creatures().crumbling().crumbled();
        const double start = threadSeconds();
        for (int tick = 0; tick < hour; ++tick) {
            m_server->tick();
            if (tick % 20 == 0) {
                clearBuild(pad);
            }
        }
        const double farmSeconds = threadSeconds() - start;
        const int golemsPerHour = m_server->creatures().crumbling().crumbled() - before;
        qInfo().noquote() << u"sand farm: %1 golems in one hour (%2 sand, %1 heads), %3 ms per tick"_s.arg(golemsPerHour)
                                 .arg(golemsPerHour * 4)
                                 .arg(1000.0 * farmSeconds / hour, 0, 'f', 4);
        QVERIFY2(golemsPerHour <= 3600 / 120 + 1, qPrintable(QString::number(golemsPerHour)));
        QVERIFY2(golemsPerHour >= 3600 / 120 - 2, qPrintable(QString::number(golemsPerHour)));
        // The farm keeps its water: no crumbled block ended in it.
        QCOMPARE(poolSources(pad), 4);

        // The same ticks without the zone.
        m_sites->clear();
        const double plainStart = threadSeconds();
        for (int tick = 0; tick < 20 * 600; ++tick) {
            m_server->tick();
        }
        qInfo().noquote() << u"without the zone: %1 ms per tick"_s.arg(1000.0 * (threadSeconds() - plainStart) / (20 * 600), 0, 'f', 4);
    }
};

QTEST_GUILESS_MAIN(TestSandGolems)
#include "test_sand_golems.moc"
