// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "QcServerTestFixture.h"

#include "common/content/registry/QcContentParser.h"
#include "QcTestUtil.h"
#include "server/weather/QcWeatherCycle.h"

#include <QJsonDocument>
#include <QMap>
#include <QTemporaryDir>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

// What the weather does to the world (stage 35.2, decision E-396): rain puts out fires and burning
// bodies under the open sky and wets farmland, snow settles in thin layers only where it snows and
// only a few columns per tick, lightning hurts the bodies near it and lights fire only within the
// guarantees of fire; how often it strikes comes from the content. Ticks are stepped by hand.
class TestWeatherEffects : public QcServerTestFixture
{
    Q_OBJECT

private:
    // A fire takes a step at least this often.
    static constexpr int stepTicks = QcFireRules::minStepTicks + QcFireRules::stepTicksSpread;

    // Ticks until the strengths of a newly set weather have fully faded in.
    void fade() { ticks(QcWeatherCycle::fadeTicks + 2); }

    void setWeather(const QString &kind)
    {
        chat(u"/gamerule weatherCycle false"_s);
        chat(u"/weather "_s + kind);
        fade();
    }

    void set(QcBlockPos pos, const char *name) { QVERIFY(m_server->world().setBlock(pos, id(name))); }
    bool isFire(QcBlockPos pos) const { return m_server->world().blockAt(pos) == id("qtcraft:fire"); }
    QcBlockId soil(bool wet) const { return m_content->withProperty(id("qtcraft:farmland"), u"moist"_s, wet ? u"true"_s : u"false"_s); }

    // A creature of `name` standing at `feet`; returns its id.
    quint32 creature(const char *name, QcBlockPos feet)
    {
        const std::optional<int> type = m_content->mobIndex(QString::fromLatin1(name));
        if (!type) {
            QcTestUtil::failCase(QStringLiteral("Test uses the creature %1, which the content does not have").arg(QString::fromLatin1(name)));
            return 0;
        }
        return m_server->mobs().spawn(*type, {feet.x + 0.5, static_cast<double>(feet.y), feet.z + 0.5}, m_server->entities().newId(), false, true);
    }

    // The content with the weather settings changed by `change`.
    template<typename Change>
    void changeWeather(Change change)
    {
        auto content = std::make_shared<QcContentRegistry>(*m_content);
        QcWeatherDefinition weather = content->weather();
        change(weather);
        content->setWeather(weather);
        m_content = std::move(content);
    }

    // Snow layers in the square of `radius` columns around the player, any height.
    QList<QcBlockPos> snowLayers(int radius) const
    {
        QList<QcBlockPos> found;
        const QcBlockId layer = id("qtcraft:snow_layer");
        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dz = -radius; dz <= radius; ++dz) {
                for (int y = 0; y < QcWorld::worldHeight; ++y) {
                    const QcBlockPos pos{m_feet.x + dx, y, m_feet.z + dz};
                    if (m_server->world().blockAt(pos) == layer) {
                        found.append(pos);
                    }
                }
            }
        }
        return found;
    }

private slots:
    void rainPutsOutFireUnderTheOpenSkyNotUnderARoof()
    {
        join();
        chat(u"/gamerule fireSpread false"_s);
        setWeather(u"rain"_s);

        // Two fires next to a log (fuel: they would burn for QcFireQueue::maxAge steps); one roofed.
        const QcBlockPos open = m_feet.offset(2, 0, 0);
        const QcBlockPos roofed = m_feet.offset(-2, 0, 0);
        set(open.offset(1, 0, 0), "qtcraft:oak_log");
        set(roofed.offset(-1, 0, 0), "qtcraft:oak_log");
        set(roofed.offset(0, 3, 0), "qtcraft:stone");
        QVERIFY(m_server->systems().weather.isRainingAt(open));
        QVERIFY(!m_server->systems().weather.isRainingAt(roofed));
        QVERIFY(m_server->systems().fire.ignite(open));
        QVERIFY(m_server->systems().fire.ignite(roofed));

        // 30 steps at most; the roofed fire lives at least 45 steps of 30 ticks.
        ticks(30 * stepTicks);
        QVERIFY(!isFire(open));
        QVERIFY(isFire(roofed));
        QCOMPARE(m_server->systems().fire.numbers().rainedOut, 1u);

        // Clear weather: a fire under the open sky burns on.
        setWeather(u"clear"_s);
        QVERIFY(m_server->systems().fire.ignite(open));
        ticks(10 * stepTicks);
        QVERIFY(isFire(open));
        QCOMPARE(m_server->systems().fire.numbers().rainedOut, 1u);
    }

    void fireDoesNotSpreadIntoTheRain()
    {
        join();
        setWeather(u"rain"_s);

        // A floor of planks around the spark: in clear weather it would catch all around.
        for (int dx = -3; dx <= 3; ++dx) {
            for (int dz = 1; dz <= 3; ++dz) {
                set(m_feet.offset(dx, -1, dz), "qtcraft:oak_planks");
            }
        }
        QVERIFY(m_server->systems().fire.spreads());
        QVERIFY(m_server->systems().fire.ignite(m_feet.offset(0, 0, 2)));
        ticks(60 * stepTicks);
        QCOMPARE(m_server->systems().fire.numbers().lit, 1u);
        QVERIFY(!isFire(m_feet.offset(0, 0, 2)));
    }

    void eternalFireBurnsInTheRain()
    {
        join();
        setWeather(u"rain"_s);
        set(m_feet.offset(2, -1, 0), "qtcraft:magma_block");
        QVERIFY(m_server->systems().fire.ignite(m_feet.offset(2, 0, 0)));
        QVERIFY(m_server->systems().weather.isRainingAt(m_feet.offset(2, 0, 0)));
        ticks(20 * stepTicks);
        QVERIFY(isFire(m_feet.offset(2, 0, 0)));
        QCOMPARE(m_server->systems().fire.numbers().rainedOut, 0u);
    }

    void rainPutsOutBurningPlayersAndCreatures()
    {
        join();
        setWeather(u"rain"_s);
        const QcBlockPos roof = m_feet.offset(0, 3, 0);
        set(roof, "qtcraft:stone");
        set(m_feet, "qtcraft:fire");
        idle(1);
        QVERIFY(m_client->burning);
        m_server->world().setBlock(m_feet, qcAirBlockId);

        // Under the roof it burns on; in the rain it goes out at once.
        idle(10);
        QVERIFY(m_client->burning);
        m_server->world().setBlock(roof, qcAirBlockId);
        idle(1);
        QVERIFY(!m_client->burning);

        // A burning creature in the rain likewise.
        const quint32 pig = creature("qtcraft:pig", m_feet.offset(2, 0, 2));
        m_server->mobs().find(pig)->burnTicks = 100;
        ticks(1);
        QCOMPARE(m_server->mobs().find(pig)->burnTicks, 0);
    }

    // Bodies standing on blocks lower than a full cell (farmland, a bottom slab) have their feet in
    // the cell of the column's top; rain still reaches them, a roof still keeps it off.
    void rainPutsOutBodiesOnLowBlocks()
    {
        join();
        setWeather(u"rain"_s);
        const auto burningOn = [this](QcBlockPos ground, const char *block, double height) {
            set(ground, block);
            const QcVec3 feet{ground.x + 0.5, ground.y + height, ground.z + 0.5};
            const quint32 pig = m_server->mobs().spawn(*m_content->mobIndex(u"qtcraft:pig"_s), feet, m_server->entities().newId(), false, true);
            m_server->mobs().find(pig)->burnTicks = 100;
            return pig;
        };
        const quint32 onFarmland = burningOn(m_feet.offset(3, 0, 0), "qtcraft:farmland", 15.0 / 16.0);
        const quint32 onSlab = burningOn(m_feet.offset(-3, 0, 0), "qtcraft:stone_slab", 0.5);
        set(m_feet.offset(0, 3, 3), "qtcraft:stone");
        const quint32 roofed = burningOn(m_feet.offset(0, 0, 3), "qtcraft:farmland", 15.0 / 16.0);
        ticks(1);
        QCOMPARE(m_server->mobs().find(onFarmland)->burnTicks, 0);
        QCOMPARE(m_server->mobs().find(onSlab)->burnTicks, 0);
        QVERIFY(m_server->mobs().find(roofed)->burnTicks > 0);
    }

    void rainWetsFarmlandUnderTheOpenSkyOnly()
    {
        QcServerConfig config;
        // Every cell of a marked section gets a random tick about every 64 ticks.
        config.limits.randomTicksPerSection = 64;
        config.limitsFromServer = true;
        join(config);

        const QcBlockPos open = m_feet.offset(2, -1, 0);
        const QcBlockPos roofed = m_feet.offset(-2, -1, 0);
        set(open, "qtcraft:farmland");
        set(roofed, "qtcraft:farmland");
        set(roofed.offset(0, 4, 0), "qtcraft:stone");
        QVERIFY(m_server->world().setBlock(open, soil(false)));
        QVERIFY(m_server->world().setBlock(roofed, soil(false)));

        // Clear weather: no water near, both stay dry (or turn back into dirt).
        ticks(600);
        QVERIFY(m_server->world().blockAt(open) != soil(true));
        QVERIFY(m_server->world().blockAt(roofed) != soil(true));

        QVERIFY(m_server->world().setBlock(open, soil(false)));
        QVERIFY(m_server->world().setBlock(roofed, soil(false)));
        // A crop on the soil lets the rain through.
        set(open.offset(0, 1, 0), "qtcraft:wheat_crop");
        setWeather(u"rain"_s);
        ticks(600);
        QCOMPARE(*m_server->world().blockAt(open), soil(true));
        QVERIFY(m_server->world().blockAt(roofed) != soil(true));
        // No water was placed anywhere.
        for (int dx = -3; dx <= 3; ++dx) {
            for (int dz = -3; dz <= 3; ++dz) {
                QVERIFY(m_server->world().blockAt(m_feet.offset(dx, 0, dz)) != id("qtcraft:water"));
            }
        }
    }

    void snowSettlesOnlyWhereItSnowsUnderTheOpenSky()
    {
        join();
        // A stone platform high above the snow height (rain climate: snow from there up), half of it
        // under a roof; the ground of the test terrain gets rain.
        const int high = QcWeather::snowHeight + 4;
        for (int dx = -8; dx <= 8; ++dx) {
            for (int dz = -8; dz <= 8; ++dz) {
                set({m_feet.x + dx, high, m_feet.z + dz}, "qtcraft:stone");
                if (dx < 0) {
                    set({m_feet.x + dx, high + 3, m_feet.z + dz}, "qtcraft:glass");
                }
            }
        }

        setWeather(u"rain"_s);
        const quint64 columnsBefore = m_server->systems().snowfall.columns();
        quint64 layers = m_server->systems().snowfall.layers();
        const int columnsPerTick = m_content->weather().snow.columnsPerTick;
        for (int tick = 0; tick < 3000; ++tick) {
            ticks(1);
            // Bounded per tick: columnsPerTick per player, each at most one layer.
            QVERIFY(m_server->systems().snowfall.layers() - layers <= quint64(columnsPerTick));
            layers = m_server->systems().snowfall.layers();
        }
        QCOMPARE(m_server->systems().snowfall.columns() - columnsBefore, quint64(3000 * columnsPerTick));
        QVERIFY2(layers >= 10, qPrintable(QString::number(layers)));

        // Only on the open stone: not on the ground in the rain, not under the glass, not twice.
        const QList<QcBlockPos> found = snowLayers(48);
        QCOMPARE(quint64(found.size()), layers);
        for (const QcBlockPos &pos : found) {
            QCOMPARE(pos.y, high + 1);
            QVERIFY(pos.x >= m_feet.x);
            QCOMPARE(*m_server->world().blockAt(pos.offset(0, -1, 0)), id("qtcraft:stone"));
        }

        // No snow without snowfall.
        setWeather(u"clear"_s);
        const quint64 clear = m_server->systems().snowfall.layers();
        ticks(500);
        QCOMPARE(m_server->systems().snowfall.layers(), clear);
    }

    void snowLayersStayWithTheWorld()
    {
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        join(config);
        // Away from the stage the join clears.
        const QcBlockPos pos = m_feet.offset(6, 0, 6);
        const int y = QcWeather::snowHeight + 4;
        set({pos.x, y, pos.z}, "qtcraft:stone");
        set({pos.x, y + 1, pos.z}, "qtcraft:snow_layer");
        QVERIFY(m_server->save().has_value());
        m_server->waitForStorage();
        m_client.reset();
        m_server.reset();

        join(config);
        QCOMPARE(*m_server->world().blockAt({pos.x, y + 1, pos.z}), id("qtcraft:snow_layer"));
        // The layer breaks into a snowball, with a shovel (decision E-427).
        QCOMPARE(m_content->block(id("qtcraft:snow_layer")).drops.first().item, u"qtcraft:snowball"_s);
        QCOMPARE(m_content->block(id("qtcraft:snow_layer")).toolLevel, 1);
    }

    void lightningHurtsTheBodiesNearIt()
    {
        join();
        chat(u"/gamerule fireSpread false"_s);
        const QcWeatherDefinition::Lightning &lightning = m_content->weather().lightning;
        const quint32 near = creature("qtcraft:pig", m_feet.offset(2, 0, 0));
        const quint32 far = creature("qtcraft:pig", m_feet.offset(-3, 0, 3).offset(-lightning.radius, 0, 0));
        const double pigHealth = m_server->mobs().find(near)->health;
        const double health = m_client->health;

        m_server->systems().weather.strike(m_feet.offset(1, 0, 0));
        QcTestUtil::pump();
        QCOMPARE(m_client->health, health - lightning.damage);
        QVERIFY(m_client->burning);
        QCOMPARE(m_server->mobs().find(near)->health, pigHealth - lightning.damage);
        QVERIFY(m_server->mobs().find(near)->burnTicks > 0);
        QCOMPARE(m_server->mobs().find(far)->health, pigHealth);
        QCOMPARE(m_server->systems().lightning.numbers().playersHit, 1u);
        QCOMPARE(m_server->systems().lightning.numbers().creaturesHit, 1u);
        // fireSpread off: no fire.
        QCOMPARE(m_server->systems().lightning.numbers().fires, 0u);
        QCOMPARE(m_server->systems().fire.numbers().lit, 0u);

        // Players in creative mode are spared.
        chat(u"/gamemode creative"_s);
        ticks(QcPhysics::tickRate * 2);
        const double creative = m_client->health;
        m_server->systems().weather.strike(m_feet);
        QcTestUtil::pump();
        QCOMPARE(m_client->health, creative);
        QCOMPARE(m_server->systems().lightning.numbers().playersHit, 1u);
    }

    void lightningLightsFireOnlyWithinTheGuarantees()
    {
        join();
        const int sparks = m_content->weather().lightning.sparks;
        // Fire spreads: the struck cell and at most sparks - 1 more catch, each as its own spark.
        m_server->systems().weather.strike(m_feet.offset(2, 0, 2));
        QVERIFY(isFire(m_feet.offset(2, 0, 2)));
        QVERIFY(m_server->systems().lightning.numbers().fires >= 1u);
        QVERIFY(std::cmp_less_equal(m_server->systems().lightning.numbers().fires, sparks));
        QCOMPARE(m_server->systems().fire.numbers().lit, m_server->systems().lightning.numbers().fires);

        // Off: no fire.
        chat(u"/gamerule fireSpread false"_s);
        const quint64 lit = m_server->systems().fire.numbers().lit;
        m_server->systems().weather.strike(m_feet.offset(-2, 0, -2));
        QVERIFY(!isFire(m_feet.offset(-2, 0, -2)));
        QCOMPARE(m_server->systems().fire.numbers().lit, lit);
    }

    void lightningRespectsTheFireCaps()
    {
        QcServerConfig config;
        config.limits.firesPerChunk = 1;
        config.limitsFromServer = true;
        join(config);

        // The chunk has its one fire: a strike in it lights nothing more.
        const QcBlockPos first = m_feet.offset(1, 0, 1);
        QVERIFY(m_server->systems().fire.ignite(first));
        QCOMPARE(m_server->systems().fire.numbers().lit, 1u);
        // The spawn lies at a chunk corner: a cell of the stage two blocks from the first fire.
        QcBlockPos struck = first;
        for (const QcBlockPos candidate : {first.offset(2, 0, 0), first.offset(-2, 0, 0), first.offset(0, 0, 2), first.offset(0, 0, -2)}) {
            if (candidate.chunk() == first.chunk() && std::abs(candidate.x - m_feet.x) <= 3 && std::abs(candidate.z - m_feet.z) <= 3) {
                struck = candidate;
            }
        }
        QVERIFY(struck != first);
        m_server->systems().weather.strike(struck);
        QCOMPARE(m_server->systems().fire.numbers().lit, 1u);
        QVERIFY(m_server->systems().fire.numbers().refused >= 1);

        // fireSpreadRadius 0 (server setting): no fire either.
        QcServerConfig still;
        still.limits.fireSpreadRadius = 0;
        still.limitsFromServer = true;
        join(still);
        m_server->systems().weather.strike(m_feet.offset(2, 0, 2));
        QCOMPARE(m_server->systems().fire.numbers().lit, 0u);
    }

    void strikesAsOftenAsTheContentSays()
    {
        QCOMPARE(m_content->weather().lightning.meanSeconds, 10.0);
        const auto strikesIn = [this](int ticksOfThunder) {
            join();
            m_server->systems().weather.setStrikeHook({});
            setWeather(u"thunder"_s);
            const quint64 before = m_server->systems().weather.strikes();
            ticks(ticksOfThunder);
            return m_server->systems().weather.strikes() - before;
        };

        const quint64 often = [&] {
            changeWeather([](QcWeatherDefinition &weather) { weather.lightning.meanSeconds = 1.0; });
            return strikesIn(2000);
        }();
        QCOMPARE(m_server->systems().weather.meanStrikeTicks(), 20);
        m_content = QcTestUtil::loadContent();
        const quint64 normal = strikesIn(2000);
        QCOMPARE(m_server->systems().weather.meanStrikeTicks(), 200);
        QVERIFY2(often > 4 * normal && often >= 20, qPrintable(u"%1 %2"_s.arg(often).arg(normal)));
    }

    // The strikes of a thunderstorm go on after saving and loading as if the world had not been
    // saved: their draws follow the seed and the weather's tick, not the server's start.
    void strikesGoOnAfterLoading()
    {
        using Strikes = QMap<qint64, QcBlockPos>;
        const auto record = [this](Strikes &strikes, int count) {
            m_server->systems().weather.setStrikeHook([this, &strikes](QcBlockPos pos) { strikes.insert(m_server->systems().weather.state().ticks, pos); });
            ticks(count);
            m_server->systems().weather.setStrikeHook({});
        };
        const auto restart = [this](const QcServerConfig &config) {
            QVERIFY(m_server->save().has_value());
            m_server->waitForStorage();
            m_client.reset();
            m_server.reset();
            join(config);
        };

        QTemporaryDir saved;
        QcServerConfig config;
        config.worldDirectory = saved.path();
        join(config);
        setWeather(u"thunder"_s);
        Strikes first;
        record(first, 3000);
        restart(config);
        Strikes afterLoading;
        record(afterLoading, 3000);

        QTemporaryDir unsaved;
        QcServerConfig through;
        through.worldDirectory = unsaved.path();
        m_client.reset();
        m_server.reset();
        join(through);
        setWeather(u"thunder"_s);
        Strikes continuous;
        record(continuous, 6100);

        QVERIFY2(first.size() >= 5 && afterLoading.size() >= 5, qPrintable(u"%1 %2"_s.arg(first.size()).arg(afterLoading.size())));
        QVERIFY(!std::ranges::equal(afterLoading, first));
        int compared = 0;
        for (const auto [tick, pos] : afterLoading.asKeyValueRange()) {
            if (tick <= continuous.lastKey()) {
                QCOMPARE(continuous.value(tick, QcBlockPos{0, -1, 0}), pos);
                ++compared;
            }
        }
        QVERIFY(compared >= 5);
    }

    void weatherSettingsParseAndRefuseNonsense()
    {
        const auto parse = [](const char *json) {
            return QcContentParser::parseWeather(QJsonDocument::fromJson(json).object(), u"mod"_s);
        };
        const QcResult<QcWeatherDefinition> parsed =
            parse(R"({"lightning": {"meanSeconds": 4, "damage": 2, "radius": 1, "burnSeconds": 0, "sparks": 0},
                      "snow": {"layer": "powder", "columnsPerTick": 0}})");
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->lightning.meanSeconds, 4.0);
        QCOMPARE(parsed->lightning.damage, 2.0);
        QCOMPARE(parsed->lightning.radius, 1);
        QCOMPARE(parsed->lightning.burnSeconds, 0);
        QCOMPARE(parsed->lightning.sparks, 0);
        QCOMPARE(parsed->snow.layer, u"mod:powder"_s);
        QCOMPARE(parsed->snow.columnsPerTick, 0);

        // Defaults for what is missing.
        QCOMPARE(parse("{}")->lightning.meanSeconds, QcWeatherDefinition{}.lightning.meanSeconds);
        QVERIFY(!parse(R"({"lightning": {"meanSeconds": 0}})").has_value());
        QVERIFY(!parse(R"({"lightning": {"sparks": 100}})").has_value());
        QVERIFY(!parse(R"({"snow": {"columnsPerTick": 1000}})").has_value());
        QVERIFY(!parse(R"({"snow": 3})").has_value());

        // The base content: as documented in docs/Modding.md.
        QCOMPARE(m_content->weather().lightning.sparks, 2);
        QCOMPARE(m_content->weather().snow.layer, u"qtcraft:snow_layer"_s);
    }
};

QTEST_GUILESS_MAIN(TestWeatherEffects)
#include "test_weather_effects.moc"
