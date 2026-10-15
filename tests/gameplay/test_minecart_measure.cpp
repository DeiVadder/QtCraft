#include "QcBlockRules.h"
#include "QcEntityChecks.h"
#include "QcMinecartMotion.h"
#include "QcServerTestFixture.h"

#include <QElapsedTimer>

#include <algorithm>

using namespace Qt::StringLiterals;

// Headless measurement of minecarts (stages 32.1 and 32.2, decisions E-382, E-387), ticked by hand as
// fast as the server goes: carts on a square loop of rails in the air, first all rolling (each kept
// at the same speed between ticks, outside the measured phase), then all standing; and chest and
// hopper carts kept rolling by powered rails, hopper carts pulling from chests above the track and
// hoppers under it unloading the carts. Prints the time of the minecart phase and of the whole tick
// per tick. Small in every test run (16 carts, checks that they stay on the loop);
// QTCRAFT_MINECART_MEASURE=full measures 200 plain carts and 100 container carts over 1000 ticks each
// (Release).
class TestMinecartMeasure : public QcServerTestFixture
{
    Q_OBJECT

private:
    struct Size
    {
        int side = 12;
        int carts = 16;
        int ticks = 20;
    };

    static bool full() { return qEnvironmentVariable("QTCRAFT_MINECART_MEASURE") == u"full"_s; }
    static Size size() { return full() ? Size{56, 200, 1000} : Size{}; }
    static Size containerSize() { return full() ? Size{56, 100, 1000} : Size{20, 16, 40}; }

    QList<QcBlockPos> m_loop;

    // A square loop of rails of `side` blocks per side on a stone floor high above the stage; with
    // `powered`, powered rails on the straights (they cannot bend) with a lever on every eighth.
    void buildLoop(int side, bool powered = false)
    {
        const QcBlockPos origin = m_feet.offset(-side / 2, 20, -side / 2);
        for (int x = -16; x <= side + 16; x += 8) {
            for (int z = -16; z <= side + 16; z += 8) {
                m_server->world().loadChunk(origin.offset(x, 0, z).chunk());
            }
        }

        for (int i = 0; i < side; ++i) {
            m_loop.append(origin.offset(i, 0, 0));
        }
        for (int i = 1; i < side; ++i) {
            m_loop.append(origin.offset(side - 1, 0, i));
        }
        for (int i = side - 2; i >= 0; --i) {
            m_loop.append(origin.offset(i, 0, side - 1));
        }
        for (int i = side - 2; i >= 1; --i) {
            m_loop.append(origin.offset(0, 0, i));
        }

        const auto corner = [&](QcBlockPos pos) {
            return (pos.x == origin.x || pos.x == origin.x + side - 1) && (pos.z == origin.z || pos.z == origin.z + side - 1);
        };
        for (const QcBlockPos &pos : std::as_const(m_loop)) {
            QVERIFY(m_server->world().setBlock(pos.offset(0, -1, 0), id("qtcraft:stone")));
            QVERIFY(m_server->world().setBlock(pos, id(powered && !corner(pos) ? "qtcraft:powered_rail" : "qtcraft:rail")));
        }
        for (const QcBlockPos &pos : std::as_const(m_loop)) {
            QVERIFY(m_server->world().setBlock(pos, QcBlockRules::connectedState(m_server->world(), pos, *m_server->world().blockAt(pos))));
        }
        if (powered) {
            placeLevers(origin, side);
        }
    }

    // Levers on the floor outside the loop beside every eighth rail but the corners.
    void placeLevers(QcBlockPos origin, int side)
    {
        const auto beyond = [&](int at, int first) { return at == first ? -1 : at == first + side - 1 ? 1 : 0; };
        for (qsizetype i = 0; i < m_loop.size(); i += 8) {
            const QcBlockPos pos = m_loop[i];
            const int dx = beyond(pos.x, origin.x);
            const int dz = beyond(pos.z, origin.z);
            if (dx != 0 && dz != 0) {
                continue;
            }
            const QcBlockPos outside = pos.offset(dx, 0, dz);
            QVERIFY(m_server->world().setBlock(outside.offset(0, -1, 0), id("qtcraft:stone")));
            QVERIFY(m_server->world().setBlock(outside, *m_content->stateId(u"qtcraft:lever[attach=floor,powered=true]"_s)));
            m_server->circuits().noteBlock(outside);
        }
    }

    // Chests with cobblestone above every tenth rail and hoppers (into chests) under every tenth
    // other one, so hopper carts pull and every container cart is unloaded on its way round.
    void addStations()
    {
        const QcBlockId hopper = *m_content->stateId(u"qtcraft:signal_hopper[facing=down]"_s);
        for (qsizetype i = 5; i < m_loop.size(); i += 10) {
            const QcBlockPos above = m_loop[i].offset(0, 1, 0);
            QVERIFY(m_server->world().setBlock(above, id("qtcraft:chest")));
            QVERIFY(ensureEntity(above));
            for (QcItemStack &stack : m_server->blockEntities().container(above)->stacks) {
                stack = {item("qtcraft:cobblestone"), 64, 0};
            }
        }
        for (qsizetype i = 0; i < m_loop.size(); i += 10) {
            const QcBlockPos below = m_loop[i].offset(0, -1, 0);
            QVERIFY(m_server->world().setBlock(below, hopper));
            QVERIFY(m_server->world().setBlock(below.offset(0, -1, 0), id("qtcraft:chest")));
        }
    }

    // Carts spread evenly along the loop; with `containers`, chest and hopper carts in turn, the chest
    // carts half full.
    void placeCarts(int count, bool containers = false)
    {
        for (int i = 0; i < count; ++i) {
            const QcBlockPos cell = m_loop[qsizetype(i) * m_loop.size() / count];
            const QString item = !containers ? u"qtcraft:minecart"_s : i % 2 == 0 ? u"qtcraft:chest_minecart"_s : u"qtcraft:hopper_minecart"_s;
            QcMinecart cart = m_server->carts().fromItem(item);
            cart.motion.position = *m_server->minecarts().restingPlace(cell);
            for (qsizetype slot = 0; i % 2 == 0 && slot < cart.stacks.size(); slot += 2) {
                cart.stacks[slot] = {this->item("qtcraft:stick"), 64, 0};
            }
            QVERIFY(m_server->minecarts().spawn(cart));
        }
    }

    // Keeps every cart rolling at `speed` the way it goes (round the loop the first time).
    void kick(double speed)
    {
        for (QcMinecart &cart : m_server->carts().editableAll()) {
            const QcVec3 velocity = cart.motion.velocity;
            if (velocity.length() > 0.0) {
                cart.motion.velocity = velocity.normalized() * speed;
                continue;
            }
            const std::optional<QcMinecartMotion::Segment> segment =
                QcMinecartMotion::segmentAt(cart.motion.position, m_server->carts().rails(), m_server->world());
            cart.motion.velocity = segment ? segment->direction * speed : QcVec3{};
        }
    }

    struct Times
    {
        double phaseMeanMs = 0.0;
        double phaseMaxMs = 0.0;
        double tickMeanMs = 0.0;
    };

    Times run(int ticks, std::optional<double> speed)
    {
        Times times;
        for (int i = 0; i < ticks; ++i) {
            if (speed) {
                kick(*speed);
            }
            QElapsedTimer timer;
            timer.start();
            m_server->tick();
            const double tickMs = double(timer.nsecsElapsed()) / 1e6;
            const double phaseMs = double(m_server->tickPhases().lastTick()[std::size_t(QcTickPhase::Minecarts)]) / 1e6;
            times.phaseMeanMs += phaseMs / ticks;
            times.phaseMaxMs = std::max(times.phaseMaxMs, phaseMs);
            times.tickMeanMs += tickMs / ticks;
        }
        return times;
    }

    void report(const char *scenario, const Times &times, int carts) const
    {
        qInfo("MEASURE minecarts %s carts=%d phase_mean_ms=%.4f phase_max_ms=%.4f tick_mean_ms=%.4f", scenario, carts, times.phaseMeanMs,
              times.phaseMaxMs, times.tickMeanMs);
    }

private slots:
    void cartsOnALoop()
    {
        const Size measured = size();
        join();
        chat(u"/gamerule mobSpawning false"_s);
        buildLoop(measured.side);
        placeCarts(measured.carts);
        QCOMPARE(m_server->carts().all().size(), qsizetype(measured.carts));

        const Times rolling = run(measured.ticks, 4.0);
        report("rolling", rolling, measured.carts);
        // Every cart is still on the loop.
        for (const QcMinecart &cart : m_server->carts().all()) {
            QVERIFY(QcMinecartMotion::segmentAt(cart.motion.position, m_server->carts().rails(), m_server->world()));
        }

        // Left alone, the carts come to rest; then they cost what standing carts cost.
        run(200, std::nullopt);
        QVERIFY(std::ranges::all_of(m_server->carts().all(), [](const QcMinecart &cart) { return cart.motion.velocity.length() == 0.0; }));
        const Times standing = run(measured.ticks, std::nullopt);
        report("standing", standing, measured.carts);
        QVERIFY(QcEntityChecks::problems(m_server->entities()).isEmpty());
    }

    void containerCartsOnAPoweredLoop()
    {
        const Size measured = containerSize();
        join();
        chat(u"/gamerule mobSpawning false"_s);
        m_loop.clear();
        buildLoop(measured.side, true);
        addStations();
        placeCarts(measured.carts, true);
        // The levers power the rails; a first push starts the carts, the powered rails keep them going.
        m_server->tick();
        QVERIFY(std::ranges::all_of(m_loop, [this](QcBlockPos pos) {
            const QcBlockId block = *m_server->world().blockAt(pos);
            return m_content->block(block).railAction != QcRailAction::Boost || m_content->property(block, u"powered"_s) == u"true"_s;
        }));
        kick(1.0);

        qsizetype moved = 0;
        Times times;
        for (int i = 0; i < measured.ticks; ++i) {
            const Times one = run(1, std::nullopt);
            times.phaseMeanMs += one.phaseMeanMs / measured.ticks;
            times.phaseMaxMs = std::max(times.phaseMaxMs, one.phaseMaxMs);
            times.tickMeanMs += one.tickMeanMs / measured.ticks;
            moved += m_server->minecarts().hoppers().lastTick().moved;
        }
        report("containers-powered", times, measured.carts);
        qInfo("MEASURE minecarts containers-powered items_moved=%lld", qlonglong(moved));
        QVERIFY(moved > 0);
        // Still rolling and on the loop.
        const qsizetype rolling = std::ranges::count_if(m_server->carts().all(), [](const QcMinecart &cart) { return cart.motion.velocity.length() > 1.0; });
        QVERIFY2(rolling > measured.carts / 2, qPrintable(QString::number(rolling)));
        for (const QcMinecart &cart : m_server->carts().all()) {
            QVERIFY(QcMinecartMotion::segmentAt(cart.motion.position, m_server->carts().rails(), m_server->world()));
        }
        QVERIFY(QcEntityChecks::problems(m_server->entities()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestMinecartMeasure)
#include "test_minecart_measure.moc"
