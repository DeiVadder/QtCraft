// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/gameplay/QcMining.h"
#include "common/physics/QcPhysics.h"
#include "fixtures/QcServerTestFixture.h"
#include "server/mobs/QcInfestation.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <cmath>

using namespace Qt::StringLiterals;

// Silverfish and infested blocks (decision E-453): breaking one lets a silverfish out, a hurt
// silverfish wakes its kin in the infested blocks around it (not in chunks that are not loaded), an
// idle one hides in stone, and the veins lie only in the mountains. Ticks are stepped by hand.
class TestSilverfish : public QcServerTestFixture
{
    Q_OBJECT

private:
    static constexpr int arena = 12;

    int silverfishType() const { return *m_content->mobIndex(u"qtcraft:silverfish"_s); }
    QcBlockId at(QcBlockPos pos) const { return *m_server->world().blockAt(pos); }

    qsizetype silverfish() const
    {
        return std::ranges::count(m_server->mobs().mobs(), silverfishType(), &QcCreature::type);
    }

    // A stone floor and open air above it, without the creatures the chunks brought.
    void joinArena(const QString &mode)
    {
        join();
        chat(u"/gamemode "_s + mode);
        loadAround(m_feet, 1);
        for (int dx = -arena; dx <= arena; ++dx) {
            for (int dz = -arena; dz <= arena; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:stone"));
                for (int dy = 0; dy <= 6; ++dy) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
        m_server->mobs().removeAll();
    }

    quint32 spawn(int dx, int dz)
    {
        const QcVec3 at{m_feet.x + dx + 0.5, static_cast<double>(m_feet.y), m_feet.z + dz + 0.5};
        return m_server->mobs().spawn(silverfishType(), at, m_server->entities().newId());
    }

    void mine(QcBlockPos pos)
    {
        send(QcMsg::StartMining{pos});
        ticks(static_cast<int>(std::ceil(QcMining::breakSeconds(m_content->block(at(pos)), nullptr) * QcPhysics::tickRate)));
        send(QcMsg::FinishMining{pos});
        ticks(1);
    }

    // Whether all columns of a chunk (every third) lie in the mountains, or none does.
    static bool wholly(const QcTerrainGenerator &generator, QcChunkPos pos, bool mountains)
    {
        for (int x = 0; x < QcWorld::chunkSize; x += 3) {
            for (int z = 0; z < QcWorld::chunkSize; z += 3) {
                const QString biome = generator.biomeAt(pos.x * QcWorld::chunkSize + x, pos.z * QcWorld::chunkSize + z);
                if ((biome == u"qtcraft:mountains"_s) != mountains) {
                    return false;
                }
            }
        }
        return true;
    }

    // Infested stone in a freshly generated chunk.
    int infestedIn(const QcTerrainGenerator &generator, QcChunkPos pos) const
    {
        const QcBlockId infested = id("qtcraft:infested_stone");
        const std::shared_ptr<QcChunk> chunk = generator.generate(pos);
        int found = 0;
        for (int y = 0; y < QcWorld::worldHeight; ++y) {
            for (int x = 0; x < QcWorld::chunkSize; ++x) {
                for (int z = 0; z < QcWorld::chunkSize; ++z) {
                    found += chunk->block(x, y, z) == infested ? 1 : 0;
                }
            }
        }
        return found;
    }

private slots:
    void infestedBlocksComeFromData()
    {
        const QcMobDefinition &fish = m_content->mobs()[silverfishType()];
        QVERIFY(fish.hidesInBlocks && fish.hostile && !fish.spawnsNaturally());
        for (const char *base : {"stone", "cobblestone", "stone_bricks"}) {
            const QcBlockDefinition &infested = m_content->block(id(qPrintable(u"qtcraft:infested_"_s + QLatin1StringView(base))));
            const QcBlockDefinition &plain = m_content->block(id(qPrintable(u"qtcraft:"_s + QLatin1StringView(base))));
            QCOMPARE(infested.infestsBlock, u"qtcraft:"_s + QLatin1StringView(base));
            QCOMPARE(infested.infestsMob, u"qtcraft:silverfish"_s);
            // It looks like its base block, drops nothing and breaks quickly even by hand.
            QCOMPARE(infested.faceTextures, plain.faceTextures);
            QVERIFY(infested.drops.isEmpty());
            QVERIFY(QcMining::canHarvest(infested, nullptr));
            QVERIFY(QcMining::breakSeconds(infested, nullptr) < 1.6);
            QVERIFY(QcMining::breakSeconds(infested, nullptr) < QcMining::breakSeconds(plain, nullptr) / 2.0);
        }
        const auto ore = std::ranges::find(m_content->worldgen().ores(), u"qtcraft:infested_stone"_s, &QcOreDefinition::name);
        QVERIFY(ore != m_content->worldgen().ores().end());
        QCOMPARE(ore->biomes, (QList<QString>{u"qtcraft:mountains"_s}));
    }

    void breakingAnInfestedBlockLetsASilverfishOut()
    {
        joinArena(u"survival"_s);
        const QcBlockPos block = m_feet.offset(2, 0, 0);
        m_server->world().setBlock(block, id("qtcraft:infested_stone"));
        mine(block);
        QCOMPARE(at(block), qcAirBlockId);
        QCOMPARE(silverfish(), 1);
        QVERIFY(m_server->items().items().isEmpty());

        // Breaking without drops (creative mode) lets nothing out.
        m_server->mobs().removeAll();
        chat(u"/gamemode creative"_s);
        m_server->world().setBlock(block, id("qtcraft:infested_cobblestone"));
        mine(block);
        QCOMPARE(at(block), qcAirBlockId);
        QCOMPARE(silverfish(), 0);
    }

    void hurtSilverfishWakeTheirKin()
    {
        // In survival mode the silverfish chase the player instead of hiding again.
        joinArena(u"survival"_s);
        const QcBlockId infested = id("qtcraft:infested_stone_bricks");
        QList<QcBlockPos> near;
        for (int dz = -4; dz <= 4; dz += 2) {
            near.append(m_feet.offset(6, -1, dz));
            m_server->world().setBlock(near.last(), infested);
        }
        // Beyond the reach and a stone block: neither wakes.
        const QcBlockPos far = m_feet.offset(QcInfestation::wakeReach + 2, -1, 0);
        m_server->world().setBlock(far, infested);
        const QcBlockPos stone = m_feet.offset(5, -1, 0);

        const quint32 bitten = spawn(0, 0);
        const auto woken = [&] { return std::ranges::count(near, qcAirBlockId, [&](QcBlockPos pos) { return at(pos); }); };
        QVERIFY(m_server->mobs().damage({.creature = bitten, .damage = 1.0, .from = m_server->mobs().find(bitten)->motion.position}) == std::nullopt);
        ticks(1);
        QVERIFY(woken() >= 1);
        QCOMPARE(silverfish(), 1 + woken());

        // Each hurt wakes about two; a few more wake all within reach, never the far one.
        for (int i = 0; i < 20 && woken() < near.size(); ++i) {
            QcCreature *mob = m_server->mobs().find(bitten);
            QVERIFY(mob);
            mob->health = 8.0;
            mob->hurt.clear();
            m_server->mobs().damage({.creature = bitten, .damage = 1.0, .from = mob->motion.position});
            ticks(1);
        }
        QCOMPARE(woken(), near.size());
        QCOMPARE(at(far), infested);
        QCOMPARE(at(stone), id("qtcraft:stone"));
        QCOMPARE(silverfish(), 1 + near.size());
    }

    void wakingStopsAtChunksThatAreNotLoaded()
    {
        // A silverfish at the east edge of its chunk; the chunk beyond is away while it is hurt.
        joinArena(u"survival"_s);
        const int border = (m_feet.chunk().x + 1) * QcWorld::chunkSize;
        const QcBlockPos inside{border - 1, m_feet.y - 1, m_feet.z};
        const QcBlockPos beyond{border, m_feet.y - 1, m_feet.z};
        for (int x = border - 4; x < border; ++x) {
            m_server->world().setBlock({x, m_feet.y - 1, m_feet.z}, id("qtcraft:stone"));
            m_server->world().setBlock({x, m_feet.y, m_feet.z}, qcAirBlockId);
            m_server->world().setBlock({x, m_feet.y + 1, m_feet.z}, qcAirBlockId);
        }
        m_server->world().setBlock(inside, id("qtcraft:infested_stone"));
        m_server->world().setBlock(beyond, id("qtcraft:infested_stone"));
        const QcChunkPos away = beyond.chunk();
        const std::shared_ptr<QcChunk> taken = m_server->world().takeChunk(away);
        QVERIFY(taken);

        const QcVec3 feet{border - 2.5, static_cast<double>(m_feet.y), m_feet.z + 0.5};
        const quint32 bitten = m_server->mobs().spawn(silverfishType(), feet, m_server->entities().newId());
        QVERIFY(m_server->mobs().damage({.creature = bitten, .damage = 1.0, .from = feet}) == std::nullopt);
        ticks(1);
        QCOMPARE(at(inside), qcAirBlockId);
        QCOMPARE(silverfish(), 2);
        QVERIFY(!m_server->world().blockAt(beyond));

        // The chunk comes back with its infested block whole.
        m_server->world().adoptStoredChunks({{away, taken}});
        ticks(1);
        QCOMPARE(at(beyond), id("qtcraft:infested_stone"));
    }

    void idleSilverfishHideInStone()
    {
        // Nobody to chase in creative mode: it slips into the stone floor or a block beside it.
        joinArena(u"creative"_s);
        m_server->world().setBlock(m_feet.offset(4, 0, 4), id("qtcraft:cobblestone"));
        spawn(3, 3);
        for (int i = 0; i < 3000 && silverfish() > 0; ++i) {
            ticks(1);
        }
        QCOMPARE(silverfish(), 0);
        int hidden = 0;
        for (int dx = -arena; dx <= arena; ++dx) {
            for (int dz = -arena; dz <= arena; ++dz) {
                for (int dy = -1; dy <= 1; ++dy) {
                    const QcBlockId block = at(m_feet.offset(dx, dy, dz));
                    hidden += block == id("qtcraft:infested_stone") || block == id("qtcraft:infested_cobblestone") ? 1 : 0;
                }
            }
        }
        QCOMPARE(hidden, 1);
    }

    void chasingSilverfishDoNotHide()
    {
        joinArena(u"survival"_s);
        spawn(3, 0);
        ticks(200);
        QCOMPARE(silverfish(), 1);
    }

    // With the game rule mobGriefing off, as in the reference, silverfish do not hide, and a hurt one
    // turns the infested blocks around it back into their base blocks instead of breaking them: no
    // silverfish comes out (decision E-453).
    void withoutMobGriefingSilverfishNeitherHideNorBreakBlocks()
    {
        joinArena(u"creative"_s);
        chat(u"/gamerule mobGriefing false"_s);
        const quint32 fish = spawn(3, 3);
        ticks(400);
        QCOMPARE(silverfish(), 1);

        const QcBlockId infested = id("qtcraft:infested_stone_bricks");
        const QcBlockPos near = m_feet.offset(6, -1, 3);
        m_server->world().setBlock(near, infested);
        QcCreature *mob = m_server->mobs().find(fish);
        QVERIFY(mob);
        for (int i = 0; i < 20 && at(near) == infested; ++i) {
            mob = m_server->mobs().find(fish);
            QVERIFY(mob);
            mob->health = 8.0;
            mob->hurt.clear();
            m_server->mobs().damage({.creature = fish, .damage = 1.0, .from = mob->motion.position});
            ticks(1);
        }
        QCOMPARE(at(near), id("qtcraft:stone_bricks"));
        QCOMPARE(silverfish(), 1);
    }

    void veinsLieOnlyInTheMountains()
    {
        const QcTerrainGenerator generator(m_content, 52);
        int mountainChunks = 0;
        int otherChunks = 0;
        int inMountains = 0;
        for (int cx = -40; cx <= 40 && (mountainChunks < 4 || otherChunks < 4); cx += 3) {
            for (int cz = -40; cz <= 40 && (mountainChunks < 4 || otherChunks < 4); cz += 3) {
                if (mountainChunks < 4 && wholly(generator, {cx, cz}, true)) {
                    inMountains += infestedIn(generator, {cx, cz});
                    ++mountainChunks;
                } else if (otherChunks < 4 && wholly(generator, {cx, cz}, false)) {
                    QCOMPARE(infestedIn(generator, {cx, cz}), 0);
                    ++otherChunks;
                }
            }
        }
        QCOMPARE(mountainChunks, 4);
        QCOMPARE(otherChunks, 4);
        // Seven veins of up to nine blocks per chunk, where they meet stone.
        QVERIFY2(inMountains >= 20, qPrintable(QString::number(inMountains)));
    }
};

QTEST_MAIN(TestSilverfish)
#include "test_silverfish.moc"
