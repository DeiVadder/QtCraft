// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/player/QcBlockForecast.h"
#include "clientcore/session/QcBlockMirror.h"
#include "fixtures/QcTestUtil.h"

#include <QTest>

using namespace Qt::StringLiterals;

// The client's block mirror with the local player's predictions (decision E-853): a prediction shows
// at once, the server's blocks for a predicted cell wait for the acknowledgement, which confirms
// without a change or rolls back to the server's block; a later prediction of the same cell outlives
// the acknowledgement of an earlier one; chunks that go and come keep or drop the predictions. And
// what the forecast expects a break or a placement to set.
class TestBlockMirror : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    QcBlockId m_stone = block(u"qtcraft:stone"_s);
    QcBlockId m_dirt = block(u"qtcraft:dirt"_s);
    // The surface: stone up to here, air above.
    static constexpr int ground = 64;

    QcBlockId block(const QString &name) const
    {
        const std::optional<QcBlockId> id = m_content->blockId(name);
        if (!id) {
            QcTestUtil::failCase(u"unknown block %1"_s.arg(name));
        }
        return id.value_or(qcAirBlockId);
    }

    std::shared_ptr<QcChunk> chunk(QcChunkPos pos) const
    {
        auto chunk = std::make_shared<QcChunk>(pos);
        for (int x = 0; x < QcWorld::chunkSize; ++x) {
            for (int z = 0; z < QcWorld::chunkSize; ++z) {
                for (int y = 0; y <= ground; ++y) {
                    chunk->setBlock(x, y, z, m_stone);
                }
            }
        }
        return chunk;
    }

    // A mirror with the content and the chunk at the origin.
    QcBlockMirror loadedMirror() const
    {
        QcBlockMirror mirror;
        std::ignore = mirror.handle(QcMsg::ContentSync{m_content});
        std::ignore = mirror.handle(QcMsg::ChunkData{chunk({0, 0})});
        return mirror;
    }

private slots:
    void aPredictionShowsAtOnce()
    {
        QcBlockMirror mirror = loadedMirror();
        const QcBlockPos pos{3, ground, 4};
        const QList<QcBlockMirror::Changed> changed = mirror.predict(1, {{pos, qcAirBlockId}});
        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed.first().first, pos);
        QCOMPARE(changed.first().second, m_stone);
        QCOMPARE(mirror.world()->blockAt(pos), qcAirBlockId);
        QVERIFY(mirror.isPredicted(pos));
        QCOMPARE(mirror.serverBlock(pos), m_stone);
    }

    void aConfirmationChangesNothing()
    {
        QcBlockMirror mirror = loadedMirror();
        const QcBlockPos pos{3, ground, 4};
        std::ignore = mirror.predict(1, {{pos, qcAirBlockId}});

        // The server's answer waits for the acknowledgement; equal to the prediction, nothing changes.
        const std::optional<QcBlockMirror::Change> update = mirror.handle(QcMsg::BlockUpdate{pos, qcAirBlockId});
        QVERIFY(update);
        QVERIFY(update->blocks.isEmpty());
        const std::optional<QcBlockMirror::Change> ack = mirror.handle(QcMsg::BlockAck{1});
        QVERIFY(ack);
        QVERIFY(ack->rolledBack.isEmpty());
        QCOMPARE(mirror.world()->blockAt(pos), qcAirBlockId);
        QCOMPARE(mirror.predictedCount(), 0);
    }

    void aRefusalRollsBack()
    {
        QcBlockMirror mirror = loadedMirror();
        const QcBlockPos pos{3, ground + 1, 4};
        std::ignore = mirror.predict(1, {{pos, m_dirt}});
        QCOMPARE(mirror.world()->blockAt(pos), m_dirt);

        // Refused without an answer for the cell (a dropped request): the block before comes back.
        const std::optional<QcBlockMirror::Change> ack = mirror.handle(QcMsg::BlockAck{1});
        QVERIFY(ack);
        QCOMPARE(ack->rolledBack.size(), 1);
        QCOMPARE(ack->rolledBack.first().first, pos);
        QCOMPARE(ack->rolledBack.first().second, m_dirt);
        QCOMPARE(mirror.world()->blockAt(pos), qcAirBlockId);
        QVERIFY(!mirror.isPredicted(pos));
    }

    void theServersLatestBlockWins()
    {
        // Another player placed stone where this one predicted dirt: the acknowledgement shows stone.
        QcBlockMirror mirror = loadedMirror();
        const QcBlockPos pos{3, ground + 1, 4};
        std::ignore = mirror.predict(1, {{pos, m_dirt}});
        std::ignore = mirror.handle(QcMsg::BlockBatch{{{pos, m_stone}}});
        QCOMPARE(mirror.world()->blockAt(pos), m_dirt);
        QCOMPARE(mirror.serverBlock(pos), m_stone);

        const std::optional<QcBlockMirror::Change> ack = mirror.handle(QcMsg::BlockAck{1});
        QVERIFY(ack);
        QCOMPARE(ack->rolledBack.size(), 1);
        QCOMPARE(mirror.world()->blockAt(pos), m_stone);
    }

    void aLaterPredictionOutlivesAnEarlierAcknowledgement()
    {
        QcBlockMirror mirror = loadedMirror();
        const QcBlockPos pos{3, ground, 4};
        const QcBlockPos other{5, ground, 5};
        std::ignore = mirror.predict(1, {{pos, qcAirBlockId}, {other, qcAirBlockId}});
        std::ignore = mirror.predict(2, {{pos, m_dirt}});
        std::ignore = mirror.handle(QcMsg::BlockUpdate{pos, qcAirBlockId});
        std::ignore = mirror.handle(QcMsg::BlockUpdate{other, qcAirBlockId});

        const std::optional<QcBlockMirror::Change> first = mirror.handle(QcMsg::BlockAck{1});
        QVERIFY(first && first->rolledBack.isEmpty());
        QVERIFY(mirror.isPredicted(pos));
        QVERIFY(!mirror.isPredicted(other));
        QCOMPARE(mirror.world()->blockAt(pos), m_dirt);

        std::ignore = mirror.handle(QcMsg::BlockUpdate{pos, m_dirt});
        const std::optional<QcBlockMirror::Change> second = mirror.handle(QcMsg::BlockAck{2});
        QVERIFY(second && second->rolledBack.isEmpty());
        QCOMPARE(mirror.predictedCount(), 0);
        QCOMPARE(mirror.world()->blockAt(pos), m_dirt);
    }

    void unloadedChunksDropTheirPredictions()
    {
        QcBlockMirror mirror = loadedMirror();
        const QcBlockPos pos{3, ground, 4};
        std::ignore = mirror.predict(1, {{pos, qcAirBlockId}});
        std::ignore = mirror.handle(QcMsg::ChunkUnload{{0, 0}});
        QCOMPARE(mirror.predictedCount(), 0);

        // A cell of a chunk that is not loaded is not predicted at all.
        QVERIFY(mirror.predict(2, {{pos, qcAirBlockId}}).isEmpty());
        QCOMPARE(mirror.predictedCount(), 0);
        const std::optional<QcBlockMirror::Change> ack = mirror.handle(QcMsg::BlockAck{2});
        QVERIFY(ack && ack->rolledBack.isEmpty());
    }

    void aChunkSentAgainKeepsThePrediction()
    {
        QcBlockMirror mirror = loadedMirror();
        const QcBlockPos pos{3, ground, 4};
        std::ignore = mirror.predict(1, {{pos, qcAirBlockId}});
        const std::optional<QcBlockMirror::Change> loaded = mirror.handle(QcMsg::ChunkData{chunk({0, 0})});
        QVERIFY(loaded && loaded->loaded);
        QCOMPARE(mirror.world()->blockAt(pos), qcAirBlockId);
        QCOMPARE(mirror.serverBlock(pos), m_stone);

        const std::optional<QcBlockMirror::Change> ack = mirror.handle(QcMsg::BlockAck{1});
        QVERIFY(ack);
        QCOMPARE(ack->rolledBack.size(), 1);
        QCOMPARE(mirror.world()->blockAt(pos), m_stone);
    }

    void breakingADoorTakesBothHalves()
    {
        QcBlockMirror mirror = loadedMirror();
        const QcBlockId door = block(u"qtcraft:oak_door"_s);
        const QcBlockPos lower{3, ground + 1, 4};
        const QcBlockId top = m_content->withProperty(door, u"half"_s, u"top"_s);
        std::ignore = mirror.predict(1, {{lower, door}, {lower.offset(0, 1, 0), top}});

        const QList<QcMsg::BlockUpdate> broken = QcBlockForecast::breaking(*mirror.world(), lower.offset(0, 1, 0));
        QCOMPARE(broken.size(), 2);
        QCOMPARE(broken[0].pos, lower.offset(0, 1, 0));
        QCOMPARE(broken[1].pos, lower);
        QCOMPARE(broken[1].block, qcAirBlockId);
        QVERIFY(QcBlockForecast::breaking(*mirror.world(), lower.offset(0, 3, 0)).isEmpty());
    }

    void placingForecastsTheServersState()
    {
        QcBlockMirror mirror = loadedMirror();
        const QcItemDefinition &dirtItem = m_content->item(*m_content->itemId(u"qtcraft:dirt"_s));
        const QcBlockPos support{3, ground, 4};
        const QcBlockRules::Aim aim{support, QcBlockFace::Up, {0.5, 1.0, 0.5}, 0.0F, 30.0F};
        const QcVec3 eye{3.5, ground + 2.6, 7.5};
        const QList<QcMsg::BlockUpdate> placed = QcBlockForecast::placing(*mirror.world(), dirtItem, aim, eye, {});
        QCOMPARE(placed.size(), 1);
        QCOMPARE(placed.first().pos, support.offset(0, 1, 0));
        QCOMPARE(placed.first().block, m_dirt);

        // Not into a body, not out of reach, nothing for an item that does more than place.
        const QcAabb body{{3.2, support.y + 1.0, 4.2}, {3.8, support.y + 2.8, 4.8}};
        QVERIFY(QcBlockForecast::placing(*mirror.world(), dirtItem, aim, eye, {body}).isEmpty());
        QVERIFY(QcBlockForecast::placing(*mirror.world(), dirtItem, aim, eye + QcVec3{0.0, 0.0, 9.0}, {}).isEmpty());
        const QcItemDefinition &bucket = m_content->item(*m_content->itemId(u"qtcraft:water_bucket"_s));
        QVERIFY(!QcBlockForecast::placesPlainly(bucket));
        QVERIFY(QcBlockForecast::placing(*mirror.world(), bucket, aim, eye, {}).isEmpty());

        // A door takes both cells, as the server sets them.
        const QcItemDefinition &doorItem = m_content->item(*m_content->itemId(u"qtcraft:oak_door"_s));
        const QList<QcMsg::BlockUpdate> door = QcBlockForecast::placing(*mirror.world(), doorItem, aim, eye, {});
        QCOMPARE(door.size(), 2);
        QCOMPARE(door[1].pos, support.offset(0, 2, 0));
        QCOMPARE(m_content->property(door[1].block, u"half"_s), u"top"_s);
    }
};

QTEST_GUILESS_MAIN(TestBlockMirror)
#include "test_block_mirror.moc"
