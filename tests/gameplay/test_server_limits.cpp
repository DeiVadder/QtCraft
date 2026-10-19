#include "QcServerTestFixture.h"

#include "common/gameplay/QcMining.h"
#include "QcTestUtil.h"
#include "QcTestWorld.h"
#include "server/core/QcRateLimiter.h"
#include "server/gameplay/QcLineOfSight.h"

#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

// Limits a server needs before strangers join: message rates and line of sight.
class TestServerLimits : public QcServerTestFixture
{
    Q_OBJECT

private:
    // The players' time of server tick n at 20 ticks per second.
    static constexpr qint64 atTick(qint64 tick) { return tick * 50'000'000; }

    // A stone wall three wide and four high two blocks east of the player, and a dirt block behind
    // it at eye level, within reach (3.5 m).
    QcBlockPos buildWallWithTargetBehind()
    {
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dy = 0; dy <= 3; ++dy) {
                m_server->world().setBlock(m_feet.offset(2, dy, dz), id("qtcraft:stone"));
            }
        }
        const QcBlockPos target = m_feet.offset(3, 1, 0);
        m_server->world().setBlock(target, id("qtcraft:dirt"));
        return target;
    }

    void removeWall()
    {
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dy = 0; dy <= 3; ++dy) {
                m_server->world().setBlock(m_feet.offset(2, dy, dz), qcAirBlockId);
            }
        }
    }

    void mine(QcBlockPos target, int tickCount)
    {
        m_client->connection.send(QcMsg::StartMining{target});
        QcTestUtil::pump();
        ticks(tickCount);
        m_client->connection.send(QcMsg::FinishMining{target});
        QcTestUtil::pump();
    }

    // Client message types that neither pay into a bucket nor are deliberately unlimited, and other
    // message types that are sorted in anyway.
    template<std::size_t... I>
    static QStringList unsortedMessages(std::index_sequence<I...>)
    {
        QStringList unsorted;
        const auto check = [&unsorted]<std::size_t Index>() {
            const QcMessage message(std::in_place_index<Index>);
            const bool limited = QcRateLimiter::kindOf(message).has_value();
            const bool unlimited = QcRateLimiter::isUnlimited(message);
            const bool fromClient = QcProtocol::isSentBy(Index, QcProtocol::Peer::Client);
            if (fromClient ? limited == unlimited : limited || unlimited) {
                unsorted.append(QString::number(Index));
            }
        };
        (check.template operator()<I>(), ...);
        return unsorted;
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void bucketsAllowTheBurstThenTheRate()
    {
        QcRateLimiter limiter;
        constexpr auto kind = QcRateLimiter::Kind::Chat;
        // Chat: 2 per second, bursts of 20.
        QCOMPARE(QcRateLimiter::limit(kind).perSecond, 2.0);
        QCOMPARE(QcRateLimiter::limit(kind).burst, 20.0);

        int allowed = 0;
        for (int i = 0; i < 30; ++i) {
            allowed += limiter.allow(kind, atTick(100)) ? 1 : 0;
        }
        QCOMPARE(allowed, 20);

        // Half a second later one more token is back (2 per second at 20 ticks per second).
        QVERIFY(!limiter.allow(kind, atTick(109)));
        QVERIFY(limiter.allow(kind, atTick(110)));
        QVERIFY(!limiter.allow(kind, atTick(110)));

        // A long pause refills to the burst, not beyond.
        allowed = 0;
        for (int i = 0; i < 30; ++i) {
            allowed += limiter.allow(kind, atTick(10'000)) ? 1 : 0;
        }
        QCOMPARE(allowed, 20);

        // Other kinds have their own buckets.
        QVERIFY(limiter.allow(QcRateLimiter::Kind::Use, atTick(10'000)));
    }

    void dropsAreReportedAtMostOncePerSecond()
    {
        QcRateLimiter limiter;
        constexpr auto kind = QcRateLimiter::Kind::Appearance;
        QCOMPARE(limiter.takeDropReport(kind, atTick(0)), quint64(0));

        for (int i = 0; i < 13; ++i) {
            limiter.allow(kind, atTick(0));
        }
        // 10 allowed, 3 dropped: the first report comes at once with everything so far.
        QCOMPARE(limiter.takeDropReport(kind, atTick(0)), quint64(3));

        // Still empty a few ticks later (2 per second refill a token every 10 ticks).
        QVERIFY(!limiter.allow(kind, atTick(2)));
        QVERIFY(!limiter.allow(kind, atTick(4)));
        QCOMPARE(limiter.takeDropReport(kind, atTick(10)), quint64(0));
        // One second after the last report the waiting drops are reported.
        QCOMPARE(limiter.takeDropReport(kind, atTick(20)), quint64(2));
        QCOMPARE(limiter.takeDropReport(kind, atTick(100)), quint64(0));
    }

    void messagesMapToTheirBuckets()
    {
        using Kind = QcRateLimiter::Kind;
        QCOMPARE(QcRateLimiter::kindOf(QcMsg::FinishMining{}), std::optional(Kind::Mining));
        QCOMPARE(QcRateLimiter::kindOf(QcMsg::UseItem{}), std::optional(Kind::Use));
        QCOMPARE(QcRateLimiter::kindOf(QcMsg::ConsumeItem{}), std::optional(Kind::Use));
        // A throw pays like any other use of an item (decision E-427).
        QCOMPARE(QcRateLimiter::kindOf(QcMsg::ThrowItem{}), std::optional(Kind::Use));
        QVERIFY(!QcRateLimiter::isUnlimited(QcMsg::ThrowItem{}));
        QCOMPARE(QcRateLimiter::kindOf(QcMsg::AttackEntity{}), std::optional(Kind::AttackEntity));
        QCOMPARE(QcRateLimiter::kindOf(QcMsg::ChatInput{}), std::optional(Kind::Chat));
        QCOMPARE(QcRateLimiter::kindOf(QcMsg::DropItem{}), std::optional(Kind::Drop));
        QCOMPARE(QcRateLimiter::kindOf(QcMsg::MoveInventoryItem{}), std::optional(Kind::Inventory));
        QCOMPARE(QcRateLimiter::kindOf(QcMsg::QuickMoveItem{}), std::optional(Kind::Inventory));
        QCOMPARE(QcRateLimiter::kindOf(QcMsg::SetAppearance{}), std::optional(Kind::Appearance));
        // A subscription is state: the latest one counts, none is dropped.
        QCOMPARE(QcRateLimiter::kindOf(QcMsg::DebugSubscribe{}), std::nullopt);
        QVERIFY(QcRateLimiter::isUnlimited(QcMsg::DebugSubscribe{}));
        QCOMPARE(QcRateLimiter::kindOf(QcMsg::PlayerInput{}), std::nullopt);
        QCOMPARE(QcRateLimiter::kindOf(QcMsg::Respawn{}), std::nullopt);
        // Only the press of a bow pays; its release and cancel only end a draw.
        using Action = QcMsg::DrawLauncher::Action;
        QCOMPARE(QcRateLimiter::kindOf(QcMsg::DrawLauncher{Action::Draw}), std::optional(Kind::Use));
        QVERIFY(!QcRateLimiter::isUnlimited(QcMsg::DrawLauncher{Action::Draw}));
        for (const Action action : {Action::Release, Action::Cancel}) {
            QCOMPARE(QcRateLimiter::kindOf(QcMsg::DrawLauncher{action}), std::nullopt);
            QVERIFY(QcRateLimiter::isUnlimited(QcMsg::DrawLauncher{action}));
        }
    }

    void everyClientMessageIsSortedIn()
    {
        // A message added to the protocol later must get a bucket or join the deliberately unlimited
        // ones; until then this lists its type tag.
        const QStringList unsorted = unsortedMessages(std::make_index_sequence<std::variant_size_v<QcMessage>>());
        QVERIFY2(unsorted.isEmpty(), qPrintable(u"Message types without a rate limit decision: "_s + unsorted.join(u", ")));
        QVERIFY(QcRateLimiter::isUnlimited(QcMsg::PlayerInput{}));
        QVERIFY(!QcRateLimiter::isUnlimited(QcMsg::QuickMoveItem{}));
        // Labels go from the server to clients only: no limit decision of their own.
        QVERIFY(!QcProtocol::isSentBy(QcMessage(QcMsg::ChunkLabels{}).index(), QcProtocol::Peer::Client));
        QCOMPARE(QcRateLimiter::kindOf(QcMsg::ChunkLabels{}), std::nullopt);
        // So is the server's debug data.
        QVERIFY(!QcProtocol::isSentBy(QcMessage(QcMsg::ServerDebug{}).index(), QcProtocol::Peer::Client));
        QCOMPARE(QcRateLimiter::kindOf(QcMsg::ServerDebug{}), std::nullopt);
    }

    void quickMoveFloodIsCutOff()
    {
        join();

        // 200 quick moves on the crafting result within one tick: the burst of 80 goes through.
        QTest::ignoreMessage(QtWarningMsg, "Rate limit: dropped 1 inventory messages from Raw");
        for (int i = 0; i < 200; ++i) {
            m_client->connection.send(QcMsg::QuickMoveItem{QcMsg::QuickMoveItem::craftingResultSlot});
        }
        QcTestUtil::pump();

        // A second later 40 tokens are back; the 41st message is dropped and the report counts the
        // 119 drops of the flood not yet reported plus this one.
        ticks(20);
        QTest::ignoreMessage(QtWarningMsg, "Rate limit: dropped 120 inventory messages from Raw");
        for (int i = 0; i < 41; ++i) {
            m_client->connection.send(QcMsg::QuickMoveItem{QcMsg::QuickMoveItem::craftingResultSlot});
        }
        QcTestUtil::pump();
        QCOMPARE(m_server->clientCount(), 1);
    }

    void droppedMiningGetsItsTrueBlockBack()
    {
        join();
        const QcBlockPos target = m_feet.offset(2, 0, 0);
        m_server->world().setBlock(target, id("qtcraft:dirt"));
        ticks(1);
        m_client->blockUpdates.clear();

        // The burst of 100 goes through without answers; each dropped position is answered once per
        // tick, and only three positions per tick.
        QTest::ignoreMessage(QtWarningMsg, "Rate limit: dropped 1 mining messages from Raw");
        for (int i = 0; i < 110; ++i) {
            m_client->connection.send(QcMsg::StartMining{target});
        }
        const QList<QcBlockPos> others{m_feet.offset(1, -1, 0), m_feet.offset(0, -1, 1), m_feet.offset(-1, -1, 0)};
        for (const QcBlockPos &other : others) {
            m_client->connection.send(QcMsg::FinishMining{other});
        }
        QcTestUtil::pump();

        QCOMPARE(m_client->blockUpdates.size(), 3);
        QCOMPARE(m_client->blockUpdates[0].pos, target);
        QCOMPARE(m_client->blockUpdates[0].block, id("qtcraft:dirt"));
        QCOMPARE(m_client->blockUpdates[1].pos, others[0]);
        QCOMPARE(m_client->blockUpdates[1].block, id("qtcraft:grass_block"));
        QCOMPARE(m_client->blockUpdates[2].pos, others[1]);

        // The next tick refills 5 tokens; the drops after them are answered again, once.
        ticks(1);
        m_client->blockUpdates.clear();
        for (int i = 0; i < 10; ++i) {
            m_client->connection.send(QcMsg::StartMining{target});
        }
        QcTestUtil::pump();
        QCOMPARE(m_client->blockUpdates.size(), 1);
        QCOMPARE(m_client->blockUpdates[0].pos, target);
    }

    void chatSpamIsDroppedWithoutAKick()
    {
        join();
        m_client->chatLines.clear();

        // 30 lines within one tick: the burst of 20 goes through, the rest is dropped and logged once.
        QTest::ignoreMessage(QtWarningMsg, "Rate limit: dropped 1 chat messages from Raw");
        for (int i = 0; i < 30; ++i) {
            m_client->connection.send(QcMsg::ChatInput{u"line %1"_s.arg(i)});
        }
        QcTestUtil::pump();
        QCOMPARE(m_client->chatLines.size(), 20);
        QCOMPARE(m_client->chatLines.constLast(), u"<Raw> line 19"_s);

        // Still connected: a second later two more lines get through; the report covers all drops since.
        ticks(20);
        QTest::ignoreMessage(QtWarningMsg, "Rate limit: dropped 10 chat messages from Raw");
        for (int i = 0; i < 3; ++i) {
            m_client->connection.send(QcMsg::ChatInput{u"later %1"_s.arg(i)});
        }
        QcTestUtil::pump();
        QCOMPARE(m_client->chatLines.size(), 22);
        ticks(1);
        QCOMPARE(m_server->clientCount(), 1);
    }

    void localPlayerChatIsNotLimited()
    {
        join();
        // The player of the in-process host joins next to the raw client, which counts as remote.
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd), QcServer::ClientOrigin::Local);
        RawClient owner(std::move(clientEnd));
        owner.hello(u"Owner"_s);
        QcTestUtil::pump();
        ticks(2);
        QCOMPARE(m_server->clientCount(), 2);
        owner.chatKeys.clear();

        // A burst of 100 commands within one tick: every one is answered, nothing is dropped.
        QTest::failOnWarning(QRegularExpression(u"Rate limit: dropped \\d+ chat messages from Owner"_s));
        for (int i = 0; i < 100; ++i) {
            owner.connection.send(QcMsg::ChatInput{u"/heal"_s});
        }
        QcTestUtil::pump();
        const auto healed = std::count_if(owner.chatKeys.cbegin(), owner.chatKeys.cend(),
                                          [](const QcMsg::ChatMessage &line) { return line.key == u"Healed"_s; });
        QCOMPARE(healed, 100);

        // Only chat is exempt: the owner's other kinds keep their buckets (inventory: bursts of 80).
        QTest::ignoreMessage(QtWarningMsg, "Rate limit: dropped 1 inventory messages from Owner");
        for (int i = 0; i < 81; ++i) {
            owner.connection.send(QcMsg::QuickMoveItem{QcMsg::QuickMoveItem::craftingResultSlot});
        }
        QcTestUtil::pump();

        // The remote client is cut off as before: 20 of 30 commands go through.
        m_client->chatKeys.clear();
        QTest::ignoreMessage(QtWarningMsg, "Rate limit: dropped 1 chat messages from Raw");
        for (int i = 0; i < 30; ++i) {
            m_client->connection.send(QcMsg::ChatInput{u"/heal"_s});
        }
        QcTestUtil::pump();
        QCOMPARE(m_client->chatKeys.size(), 20);
        QCOMPARE(m_server->clientCount(), 2);
    }

    void serverRepliesCarryTranslationKeys()
    {
        join();
        m_client->chatKeys.clear();
        chat(u"/give no_such_item 2"_s);
        chat(u"/heal"_s);
        chat(u"hello %1"_s);

        QCOMPARE(m_client->chatKeys.size(), 3);
        const QcMsg::ChatMessage &unknown = m_client->chatKeys[0];
        QCOMPARE(unknown.key, u"Unknown item or invalid count: %1"_s);
        QCOMPARE(unknown.arguments, QStringList{u"/give no_such_item 2"_s});
        QCOMPARE(unknown.text, u"Unknown item or invalid count: /give no_such_item 2"_s);
        QCOMPARE(m_client->chatKeys[1].key, u"Healed"_s);
        // Player chat is not translated.
        QVERIFY(m_client->chatKeys[2].key.isEmpty());
        QCOMPARE(m_client->chatKeys[2].text, u"<Raw> hello %1"_s);
    }

    void miningNeedsALineOfSight()
    {
        join();
        const QcBlockPos target = buildWallWithTargetBehind();
        // Dirt by hand: 0.5 * 1.5 s = 15 ticks.
        const int enough = int(QcMining::breakSeconds(m_content->block(id("qtcraft:dirt")), nullptr) * QcPhysics::tickRate) + 2;

        mine(target, enough);
        QCOMPARE(m_server->world().blockAt(target), std::optional<QcBlockId>(id("qtcraft:dirt")));

        // The same action with the wall gone succeeds: only the line of sight was missing.
        removeWall();
        mine(target, enough);
        QCOMPARE(m_server->world().blockAt(target), std::optional<QcBlockId>(qcAirBlockId));
    }

    void placingNeedsALineOfSight()
    {
        join();
        const QcBlockPos target = buildWallWithTargetBehind();
        chat(u"/give dirt 2"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:dirt"))});

        // On top of the hidden block.
        send(QcMsg::UseItem{target, QcBlockFace::Up, {0.5, 1.0, 0.5}});
        QCOMPARE(m_server->world().blockAt(target.offset(0, 1, 0)), std::optional<QcBlockId>(qcAirBlockId));
        QCOMPARE(countOf(item("qtcraft:dirt")), 2);

        removeWall();
        send(QcMsg::UseItem{target, QcBlockFace::Up, {0.5, 1.0, 0.5}});
        QCOMPARE(m_server->world().blockAt(target.offset(0, 1, 0)), std::optional<QcBlockId>(id("qtcraft:dirt")));
        QCOMPARE(countOf(item("qtcraft:dirt")), 1);
    }

    void attacksNeedALineOfSight()
    {
        join();
        buildWallWithTargetBehind();
        // A pig behind the wall; no tick runs, so it stays there.
        const QcBlockPos pen = m_feet.offset(3, 0, 1);
        const quint32 pig = m_server->mobs().spawn(*m_content->mobIndex(u"qtcraft:pig"_s), {pen.x + 0.5, double(pen.y), pen.z + 0.5}, 600);
        const double health = m_server->mobs().find(pig)->health;

        m_client->connection.send(QcMsg::AttackEntity{pig});
        QcTestUtil::pump();
        QCOMPARE(m_server->mobs().find(pig)->health, health);

        removeWall();
        m_client->connection.send(QcMsg::AttackEntity{pig});
        QcTestUtil::pump();
        QVERIFY(m_server->mobs().find(pig)->health < health);
    }

    void cornersCountAsSeen()
    {
        // From above-left of a block whose centre is hidden behind a neighbour, its top still shows.
        m_server.reset();
        QcTestWorld world(*m_content);
        world.set({1, 0, 0}, id("qtcraft:stone"));
        world.set({2, 0, 0}, id("qtcraft:stone"));
        const QcVec3 eye{0.5, 1.2, 0.5};
        QVERIFY(QcLineOfSight::reaches(world, eye, QcLineOfSight::blockBox({2, 0, 0}), QcBlockPos{2, 0, 0}));

        // Below the top surface, the neighbour hides the block completely.
        const QcVec3 low{0.5, 0.5, 0.5};
        QVERIFY(!QcLineOfSight::reaches(world, low, QcLineOfSight::blockBox({2, 0, 0}), QcBlockPos{2, 0, 0}));
        // The neighbour itself is seen.
        QVERIFY(QcLineOfSight::reaches(world, low, QcLineOfSight::blockBox({1, 0, 0}), QcBlockPos{1, 0, 0}));
    }

    void theEyeSeesOutOfItsOwnCell()
    {
        // The eye inside sugar cane: its box would stop every ray at distance 0.
        m_server.reset();
        QcTestWorld world(*m_content);
        world.set({0, 1, 0}, id("qtcraft:sugar_cane"));
        world.set({2, 1, 0}, id("qtcraft:stone"));
        const QcVec3 eye{0.5, 1.62, 0.5};
        QVERIFY(QcLineOfSight::reaches(world, eye, QcLineOfSight::blockBox({2, 1, 0}), QcBlockPos{2, 1, 0}));
        QVERIFY(QcLineOfSight::reaches(world, eye, QcAabb{{2.2, 2.0, 0.2}, {2.8, 2.9, 0.8}}));
        // The cane itself stays a target.
        QVERIFY(QcLineOfSight::reaches(world, eye, QcLineOfSight::blockBox({0, 1, 0}), QcBlockPos{0, 1, 0}));

        // Only the eye's own cell is skipped: a wall right in front still hides the stone.
        world.set({1, 1, 0}, id("qtcraft:stone"));
        QVERIFY(!QcLineOfSight::reaches(world, {0.5, 1.5, 0.5}, QcLineOfSight::blockBox({2, 1, 0}), QcBlockPos{2, 1, 0}));
    }

    void actionsWorkWithTheEyeInSugarCane()
    {
        join();
        const QcBlockPos eyeCell = m_feet.offset(0, 1, 0);
        const QcBlockPos target = m_feet.offset(2, 1, 0);
        m_server->world().setBlock(target, id("qtcraft:dirt"));
        const quint32 pig = m_server->mobs().spawn(*m_content->mobIndex(u"qtcraft:pig"_s), {m_feet.x + 0.5, double(m_feet.y), m_feet.z + 2.5}, 600);
        const double health = m_server->mobs().find(pig)->health;
        const int enough = int(QcMining::breakSeconds(m_content->block(id("qtcraft:dirt")), nullptr) * QcPhysics::tickRate) + 2;

        // The cane is put in after the ticks of mining, so no plant rule removes it first.
        m_client->connection.send(QcMsg::StartMining{target});
        QcTestUtil::pump();
        ticks(enough);
        m_server->world().setBlock(m_feet, id("qtcraft:sugar_cane"));
        m_server->world().setBlock(eyeCell, id("qtcraft:sugar_cane"));
        m_client->connection.send(QcMsg::FinishMining{target});
        m_client->connection.send(QcMsg::AttackEntity{pig});
        QcTestUtil::pump();

        QCOMPARE(m_server->world().blockAt(target), std::optional<QcBlockId>(qcAirBlockId));
        QVERIFY(m_server->mobs().find(pig)->health < health);
        QCOMPARE(m_server->world().blockAt(eyeCell), std::optional<QcBlockId>(id("qtcraft:sugar_cane")));
    }
};

QTEST_GUILESS_MAIN(TestServerLimits)
#include "test_server_limits.moc"
