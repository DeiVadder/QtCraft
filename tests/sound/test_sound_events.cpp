#include "common/physics/QcPhysics.h"
#include "common/protocol/QcConnection.h"
#include "common/protocol/QcInProcessTransport.h"
#include "QcTestUtil.h"
#include "server/core/QcServer.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"

#include "QcTestLogin.h"

#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// A raw protocol client that records the sounds and inventories the server sends.
class SoundClient
{
public:
    explicit SoundClient(std::unique_ptr<QcTransport> transport)
        : connection(std::move(transport), QcProtocol::Peer::Server, [this](QcMessage &&message) { received(std::move(message)); })
    {
    }

    // Hello, and the login answered with a key made from the name (QcTestLogin).
    void hello(const QString &name)
    {
        key = QcTestLogin::keyFor(name);
        connection.send(QcMsg::Hello{QcProtocol::version, name, {}, {}});
    }

    void received(QcMessage &&message)
    {
        if (const auto *challenge = std::get_if<QcMsg::LoginChallenge>(&message)) {
            connection.send(QcTestLogin::answer(*challenge, {key, {}, std::nullopt, 0}));
        } else if (const auto *sound = std::get_if<QcMsg::SoundEvent>(&message)) {
            sounds.append(*sound);
        } else if (const auto *state = std::get_if<QcMsg::PlayerState>(&message)) {
            motion = state->motion;
        } else if (const auto *inventory = std::get_if<QcMsg::InventoryState>(&message)) {
            stacks = inventory->stacks;
        }
    }

    QList<QcMsg::SoundEvent> soundsNamed(const QString &key) const
    {
        QList<QcMsg::SoundEvent> matching;
        for (const QcMsg::SoundEvent &sound : sounds) {
            if (sound.key == key) {
                matching.append(sound);
            }
        }
        return matching;
    }

    QcConnection connection;
    QByteArray key;
    QList<QcMsg::SoundEvent> sounds;
    QcEntityMotion motion;
    QList<QcItemStack> stacks;
};

QcVec3 centreOf(QcBlockPos pos)
{
    return {pos.x + 0.5, pos.y + 0.5, pos.z + 0.5};
}

} // namespace

// The server tells clients about sounds in the world: what happens to blocks, creatures, players
// and machines, only to players within the sound's range.
class TestSoundEvents : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    std::unique_ptr<QcServer> m_server;
    std::unique_ptr<SoundClient> m_client;
    QcBlockPos m_feet;

    QcBlockId id(const char *name) const { return *m_content->blockId(QString::fromLatin1(name)); }
    QcItemId item(const char *name) const { return *m_content->itemId(QString::fromLatin1(name)); }

    void ticks(int count) { QcTestUtil::ticks(*m_server, count); }

    void send(const QcMessage &message)
    {
        m_client->connection.send(message);
        QcTestUtil::pump();
        ticks(1);
    }

    void chat(const QString &text) { send(QcMsg::ChatInput{text}); }

    int slotOf(QcItemId wanted) const
    {
        for (int slot = 0; slot < m_client->stacks.size(); ++slot) {
            if (m_client->stacks[slot].item == wanted && !m_client->stacks[slot].isEmpty()) {
                return slot;
            }
        }
        return -1;
    }

    void join()
    {
        QcServerConfig config;
        config.viewDistance = 2;
        config.operators = {u"Ears"_s};
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_server->openWorld().has_value());

        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        m_client = std::make_unique<SoundClient>(std::move(clientEnd));
        m_client->hello(u"Ears"_s);
        QcTestUtil::pump();
        ticks(2);

        const QcVec3 p = m_client->motion.position;
        m_feet = {int(std::floor(p.x)), int(std::floor(p.y)), int(std::floor(p.z))};
        for (int cx = -2; cx <= 2; ++cx) {
            for (int cz = -2; cz <= 2; ++cz) {
                m_server->world().loadChunk({m_feet.chunk().x + cx, m_feet.chunk().z + cz});
            }
        }

        // A flat stage around the player.
        for (int dx = -4; dx <= 4; ++dx) {
            for (int dz = -4; dz <= 4; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:grass_block"));
                for (int dy = 0; dy <= 3; ++dy) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
        m_client->sounds.clear();
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void minedAndPlacedBlocksSoundByTheirMaterial()
    {
        join();
        const QcBlockPos target = m_feet.offset(1, 0, 0);
        QVERIFY(m_server->world().setBlock(target, id("qtcraft:dirt")));
        m_client->connection.send(QcMsg::StartMining{target});
        QcTestUtil::pump();

        // Dirt by hand: 0.5 * 1.5 s = 15 ticks.
        ticks(16);
        send(QcMsg::FinishMining{target});
        QCOMPARE(m_server->world().blockAt(target), std::optional<QcBlockId>(qcAirBlockId));
        const QList<QcMsg::SoundEvent> broken = m_client->soundsNamed(u"qtcraft:block.dirt.break"_s);
        QCOMPARE(broken.size(), 1);
        QCOMPARE(broken[0].position, centreOf(target));
        QCOMPARE(broken[0].volume, 1.0f);
        QCOMPARE(broken[0].pitch, 1.0f);

        chat(u"/give oak_planks 2"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:oak_planks"))});
        const QcBlockPos placed = m_feet.offset(-2, 0, 0);
        send(QcMsg::UseItem{placed.offset(0, -1, 0), QcBlockFace::Up});
        QCOMPARE(m_server->world().blockAt(placed), std::optional<QcBlockId>(id("qtcraft:oak_planks")));
        const QList<QcMsg::SoundEvent> place = m_client->soundsNamed(u"qtcraft:block.wood.place"_s);
        QCOMPARE(place.size(), 1);
        QCOMPARE(place[0].position, centreOf(placed));

        // A refused placement (into the player) makes no sound.
        const auto blockSounds = [this] {
            return std::ranges::count_if(m_client->sounds, [](const QcMsg::SoundEvent &sound) { return sound.key.startsWith(u"qtcraft:block."_s); });
        };
        const auto before = blockSounds();
        send(QcMsg::UseItem{m_feet.offset(0, -1, 0), QcBlockFace::Up});
        QCOMPARE(blockSounds(), before);
    }

    void soundsCarryOnlyAsFarAsTheirRange()
    {
        join();
        // Dirt breaks with a range of 16 m: heard at 10 m, not at 20 m.
        const QcBlockPos near = m_feet.offset(10, 0, 0);
        const QcBlockPos far = m_feet.offset(0, 0, 20);
        for (const QcBlockPos pos : {near, far}) {
            QVERIFY(m_server->world().setBlock(pos, id("qtcraft:dirt")));
            chat(u"/setblock %1 %2 %3 air"_s.arg(pos.x).arg(pos.y).arg(pos.z));
            QCOMPARE(m_server->world().blockAt(pos), std::optional<QcBlockId>(qcAirBlockId));
        }
        const QList<QcMsg::SoundEvent> heard = m_client->soundsNamed(u"qtcraft:block.dirt.break"_s);
        QCOMPARE(heard.size(), 1);
        QCOMPARE(heard[0].position, centreOf(near));

        // Water has no sound material: removing it is silent.
        const QcBlockPos pool = m_feet.offset(2, 0, 2);
        QVERIFY(m_server->world().setBlock(pool, id("qtcraft:water")));
        m_client->sounds.clear();
        chat(u"/setblock %1 %2 %3 air"_s.arg(pool.x).arg(pool.y).arg(pool.z));
        QVERIFY(m_client->sounds.isEmpty());
    }

    void creaturesSoundWhenHurtKilledAndNowAndThen()
    {
        join();

        // A pig in a pit next to the player cannot run away (as in the server validation test).
        const QcBlockPos pit = m_feet.offset(1, -1, 0);
        m_server->world().setBlock(pit, qcAirBlockId);
        m_server->world().setBlock(pit.offset(0, -1, 0), qcAirBlockId);
        m_server->world().setBlock(pit.offset(0, -2, 0), id("qtcraft:stone"));

        const quint32 pig = m_server->mobs().spawn(*m_content->mobIndex(u"qtcraft:pig"_s), {pit.x + 0.5, double(pit.y - 1), pit.z + 0.5}, 500);
        ticks(1);
        m_client->sounds.clear();
        m_client->connection.send(QcMsg::AttackEntity{pig});
        QcTestUtil::pump();
        QCOMPARE(m_client->soundsNamed(u"qtcraft:mob.pig.hurt"_s).size(), 1);

        // A second hit within the invulnerability does nothing and is silent.
        ticks(2);
        m_client->connection.send(QcMsg::AttackEntity{pig});
        QcTestUtil::pump();
        QCOMPARE(m_client->soundsNamed(u"qtcraft:mob.pig.hurt"_s).size(), 1);
        for (int attempt = 0; attempt < 15 && m_server->mobs().find(pig); ++attempt) {
            ticks(12);
            m_client->connection.send(QcMsg::AttackEntity{pig});
            QcTestUtil::pump();
        }
        QVERIFY(!m_server->mobs().find(pig));

        // Ten hits of 1 on 10 health: nine hurt the pig, the last one kills it.
        QCOMPARE(m_client->soundsNamed(u"qtcraft:mob.pig.hurt"_s).size(), 9);
        const QList<QcMsg::SoundEvent> death = m_client->soundsNamed(u"qtcraft:mob.pig.death"_s);
        QCOMPARE(death.size(), 1);
        QVERIFY((death[0].position - centreOf(pit)).length() < 2.0);

        // Living creatures with an ambient sound make it about every ten seconds each: four pigs, each
        // in its own pit, for 20 s give about eight.
        m_server->mobs().removeAll();
        for (int i = 0; i < 4; ++i) {
            const QcBlockPos hole = m_feet.offset(-3 + 2 * i, -1, 3);
            m_server->world().setBlock(hole, qcAirBlockId);
            m_server->world().setBlock(hole.offset(0, -1, 0), qcAirBlockId);
            m_server->world().setBlock(hole.offset(0, -2, 0), id("qtcraft:stone"));
            m_server->mobs().spawn(*m_content->mobIndex(u"qtcraft:pig"_s), {hole.x + 0.5, double(hole.y - 1), hole.z + 0.5}, 600 + i);
        }

        m_client->sounds.clear();
        ticks(20 * 20);
        int grunts = 0;
        const QList<QcMsg::SoundEvent> ambient = m_client->soundsNamed(u"qtcraft:mob.pig.ambient"_s);
        for (const QcMsg::SoundEvent &sound : ambient) {
            // Only the penned pigs, not pigs that spawned on their own further away.
            grunts += std::abs(sound.position.z - (m_feet.z + 3.5)) < 1.0 ? 1 : 0;
        }
        QVERIFY2(grunts >= 2 && grunts <= 20, qPrintable(QString::number(grunts)));
    }

    void playersSoundWhenHurtEatingAndPickingUp()
    {
        join();
        chat(u"/feed 10"_s);
        chat(u"/give cooked_porkchop"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:cooked_porkchop"))});
        send(QcMsg::ConsumeItem{});
        const QList<QcMsg::SoundEvent> eat = m_client->soundsNamed(u"qtcraft:player.eat"_s);
        QCOMPARE(eat.size(), 1);
        QCOMPARE(eat[0].position, (m_client->motion.position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0}));

        // An item at the player's feet is picked up once its pickup delay is over.
        m_server->items().spawnScattered({item("qtcraft:stick"), 3, 0}, m_client->motion.position + QcVec3{0.0, 0.3, 0.0}, 9000);
        ticks(40);
        const QList<QcMsg::SoundEvent> pickup = m_client->soundsNamed(u"qtcraft:item.pickup"_s);
        QCOMPARE(pickup.size(), 1);
        QCOMPARE(pickup[0].volume, 0.6f);

        chat(u"/kill"_s);
        QCOMPARE(m_client->soundsNamed(u"qtcraft:player.death"_s).size(), 1);
        QVERIFY(m_client->soundsNamed(u"qtcraft:player.hurt"_s).isEmpty());
    }

    void burningFurnacesCrackle()
    {
        join();
        chat(u"/give furnace"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:furnace"))});
        const QcBlockPos furnace = m_feet.offset(2, 0, 0);
        send(QcMsg::UseItem{furnace.offset(0, -1, 0), QcBlockFace::Up});

        // The player looks north, so the furnace's front faces south, back at the player.
        QCOMPARE(m_server->world().blockAt(furnace),
                 std::optional<QcBlockId>(m_content->withProperty(id("qtcraft:furnace"), u"facing"_s, u"south"_s)));
        // A cold furnace is silent.
        m_client->sounds.clear();
        ticks(200);
        QVERIFY(m_client->soundsNamed(u"qtcraft:machine.furnace"_s).isEmpty());

        chat(u"/give raw_iron 8"_s);
        chat(u"/give coal 1"_s);
        send(QcMsg::UseItem{furnace, QcBlockFace::West});
        send(QcMsg::MoveInventoryItem{slotOf(item("qtcraft:raw_iron")), QcMsg::MoveInventoryItem::machineInputSlot, 0});
        send(QcMsg::MoveInventoryItem{slotOf(item("qtcraft:coal")), QcMsg::MoveInventoryItem::machineFuelSlot, 0});
        send(QcMsg::CloseMachine{});
        QVERIFY(m_server->blockEntities().machine(furnace)->burnRemaining > 0.0);
        m_client->sounds.clear();
        ticks(20 * 20);
        const QList<QcMsg::SoundEvent> crackles = m_client->soundsNamed(u"qtcraft:machine.furnace"_s);

        // One crackle per 2.5 s on average: about 8 in 20 s.
        QVERIFY2(crackles.size() >= 3 && crackles.size() <= 16, qPrintable(QString::number(crackles.size())));
        for (const QcMsg::SoundEvent &crackle : crackles) {
            QCOMPARE(crackle.position, centreOf(furnace));
        }
    }
};

QTEST_GUILESS_MAIN(TestSoundEvents)
#include "test_sound_events.moc"
