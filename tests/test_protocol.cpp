#include "QcProtocol.h"

#include <QDataStream>
#include <QIODevice>
#include <QTest>

#include <limits>

using namespace Qt::StringLiterals;

namespace {

template<typename T>
T roundTrip(const T &message)
{
    const QcMessage wrapped(message);
    const QcProtocol::Peer sender =
        QcProtocol::isSentBy(wrapped.index(), QcProtocol::Peer::Client) ? QcProtocol::Peer::Client : QcProtocol::Peer::Server;
    const QcResult<QcMessage> decoded = QcProtocol::decode(QcProtocol::encode(wrapped), sender);
    if (!decoded) {
        qFatal("decode failed: %s", qPrintable(decoded.error()));
    }
    if (!std::holds_alternative<T>(*decoded)) {
        qFatal("decoded wrong alternative");
    }
    return std::get<T>(*decoded);
}

QByteArray frameWithTag(quint16 tag, const QByteArray &payload)
{
    QByteArray frame;
    QDataStream stream(&frame, QIODevice::WriteOnly);
    stream << tag;
    frame.append(payload);
    return frame;
}

} // namespace

class TestProtocol : public QObject
{
    Q_OBJECT

private slots:
    void simpleMessagesRoundTrip()
    {
        const QcMsg::Hello hello = roundTrip(QcMsg::Hello{QcProtocol::version, u"Jö"_s, {}});
        QCOMPARE(hello.protocolVersion, QcProtocol::version);
        QCOMPARE(hello.playerName, u"Jö"_s);

        const QcMsg::Welcome welcome = roundTrip(QcMsg::Welcome{1, 42, u"World"_s, -1234567890123});
        QCOMPARE(welcome.playerEntityId, 42u);
        QCOMPARE(welcome.worldName, u"World"_s);
        QCOMPARE(welcome.seed, -1234567890123);

        QCOMPARE(roundTrip(QcMsg::ChunkUnload{{-5, 9}}).pos, (QcChunkPos{-5, 9}));
        const QcMsg::BlockUpdate update = roundTrip(QcMsg::BlockUpdate{{-1, 200, 33}, 17});
        QCOMPARE(update.pos, (QcBlockPos{-1, 200, 33}));
        QCOMPARE(update.block, 17u);
        QCOMPARE(roundTrip(QcMsg::Disconnect{u"bye"_s}).reason, u"bye"_s);
    }

    void movementMessagesRoundTripExactly()
    {
        QcPlayerInput input;
        input.sequence = 99;
        input.forward = 1.0f;
        input.strafe = -0.5f;
        input.jump = true;
        input.sprint = true;
        input.yaw = 123.25f;
        input.pitch = -12.5f;
        QCOMPARE(roundTrip(QcMsg::PlayerInput{input}).input, input);

        QcMsg::PlayerState state;
        state.lastInputSequence = 99;
        state.motion.position = {1.5, 64.0, -3.25};
        state.motion.velocity = {0.1, -9.5, 0.0};
        state.motion.onGround = true;
        const QcMsg::PlayerState copy = roundTrip(state);
        QCOMPARE(copy.lastInputSequence, 99u);
        QCOMPARE(copy.motion, state.motion);
    }

    void chunkRoundTripPreservesEveryBlock()
    {
        auto chunk = std::make_shared<QcChunk>(QcChunkPos{-7, 12});
        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                for (int y = 0; y < 60; ++y) {
                    chunk->setBlock(x, y, z, QcBlockId(1 + (x + y + z) % 3));
                }
            }
        }
        chunk->section(10).fill(5);
        const QcMsg::ChunkData copy = roundTrip(QcMsg::ChunkData{chunk});
        QCOMPARE(copy.chunk->pos(), (QcChunkPos{-7, 12}));
        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                for (int y = 0; y < QcWorld::worldHeight; ++y) {
                    QCOMPARE(copy.chunk->block(x, y, z), chunk->block(x, y, z));
                }
            }
        }
        QVERIFY(copy.chunk->section(10).isUniform());
        QVERIFY(copy.chunk->section(12).isEmpty());
    }

    void contentSyncRoundTrip()
    {
        auto content = std::make_shared<QcContentRegistry>();
        QcBlockDefinition stone;
        stone.name = u"a:stone"_s;
        stone.faceTextures.fill(u"a:stone"_s);
        QVERIFY(content->registerBlock(stone).has_value());
        QcRecipeDefinition shaped;
        shaped.name = u"a:pick"_s;
        shaped.pattern = {u"SS"_s, u" S"_s};
        shaped.keys = {{u'S', u"a:stone"_s}};
        shaped.resultItem = u"a:stone"_s;
        shaped.resultCount = 3;
        content->registerRecipe(shaped);
        QcRecipeDefinition shapeless;
        shapeless.name = u"a:mix"_s;
        shapeless.shaped = false;
        shapeless.ingredients = {u"a:stone"_s, u"a:stone"_s};
        shapeless.resultItem = u"a:stone"_s;
        content->registerRecipe(shapeless);
        const QcMsg::ContentSync copy = roundTrip(QcMsg::ContentSync{content});
        QCOMPARE(copy.content->blockCount(), 2);
        QCOMPARE(copy.content->blockId(u"a:stone"_s), std::optional<QcBlockId>(1));
        QCOMPARE(copy.content->recipes().size(), 2);
        const QcRecipeDefinition &pick = copy.content->recipes()[0];
        QCOMPARE(pick.pattern, shaped.pattern);
        QCOMPARE(pick.keys, shaped.keys);
        QCOMPARE(pick.resultCount, 3);
        QCOMPARE(copy.content->recipes()[1].ingredients, shapeless.ingredients);
        QVERIFY(!copy.content->recipes()[1].shaped);

        content->addPack({u"qtcraft"_s, u"QtCraft"_s, u"0.1.0"_s});
        content->addPack({u"rubies"_s, u"Rubies"_s, u"1.0.0"_s});
        QCOMPARE(roundTrip(QcMsg::ContentSync{content}).content->packs(), content->packs());

        // A shapeless recipe without ingredients would match an empty grid on the client.
        shapeless.ingredients.clear();
        content->registerRecipe(shapeless);
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::ContentSync{content}), QcProtocol::Peer::Client).has_value());
    }

    void soundEventsRoundTripAndAreValidated()
    {
        const QcMsg::SoundEvent event = roundTrip(QcMsg::SoundEvent{u"qtcraft:block.stone.break"_s, {10.5, 64.5, -3.5}, 0.75f, 1.25f});
        QCOMPARE(event.key, u"qtcraft:block.stone.break"_s);
        QCOMPARE(event.position, (QcVec3{10.5, 64.5, -3.5}));
        QCOMPARE(event.volume, 0.75f);
        QCOMPARE(event.pitch, 1.25f);
        // The limits themselves are accepted.
        QCOMPARE(roundTrip(QcMsg::SoundEvent{u"a:b"_s, {}, QcMsg::SoundEvent::maxVolume, QcMsg::SoundEvent::minPitch}).volume, 4.0f);

        const float nan = std::numeric_limits<float>::quiet_NaN();
        const QList<QcMsg::SoundEvent> bad = {
            {QString(), {}, 1.0f, 1.0f},
            {QString(QcMsg::SoundEvent::maxKeyLength + 1, u'a'), {}, 1.0f, 1.0f},
            {u"a:b"_s, {std::numeric_limits<double>::infinity(), 0.0, 0.0}, 1.0f, 1.0f},
            {u"a:b"_s, {}, nan, 1.0f},
            {u"a:b"_s, {}, -0.1f, 1.0f},
            {u"a:b"_s, {}, 4.5f, 1.0f},
            {u"a:b"_s, {}, 1.0f, nan},
            {u"a:b"_s, {}, 1.0f, 0.2f},
            {u"a:b"_s, {}, 1.0f, 5.0f},
        };
        for (const QcMsg::SoundEvent &event : bad) {
            QVERIFY2(!QcProtocol::decode(QcProtocol::encode(event), QcProtocol::Peer::Server).has_value(), qPrintable(event.key.left(20)));
        }
        // Only the server sends sounds.
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::SoundEvent{u"a:b"_s, {}, 1.0f, 1.0f}), QcProtocol::Peer::Client).has_value());
    }

    void contentSyncCarriesSounds()
    {
        auto content = std::make_shared<QcContentRegistry>();
        QcBlockDefinition furnace;
        furnace.name = u"a:furnace"_s;
        furnace.faceTextures.fill(u"a:furnace"_s);
        furnace.soundMaterial = u"a:metal"_s;
        furnace.soundKeys[std::size_t(QcBlockSoundAction::Hit)] = u"a:clank"_s;
        furnace.machine = {u"smelting"_s, 1.0, u"a:roar"_s};
        QVERIFY(content->registerBlock(furnace).has_value());
        QcMobDefinition mob;
        mob.name = u"a:bee"_s;
        mob.ambientSound = u"a:buzz"_s;
        mob.hurtSound = u"a:ouch"_s;
        mob.deathSound = u"a:gone"_s;
        content->registerMob(mob);
        QcSoundDefinition buzz;
        buzz.name = u"a:buzz"_s;
        buzz.bus = QcSoundBus::Ambient;
        buzz.volume = 0.4;
        buzz.pitchVariation = 0.2;
        buzz.variants = 3;
        buzz.loop = true;
        buzz.range = 24.0;
        buzz.layers = {{QcSoundWave::Saw, 220.0, 330.0, 0.1, 1.5, 0.2, 1.0, 0.5, 2000.0, 100.0, 4.0, 0.3, 50.0},
                       {QcSoundWave::Crackle, 440.0, 0.0, 0.0, 0.5, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 900.0}};
        QVERIFY(buzz.problem().isEmpty());
        content->registerSound(buzz);

        const QcMsg::ContentSync copy = roundTrip(QcMsg::ContentSync{content});
        const QcBlockDefinition &block = copy.content->block(*copy.content->blockId(u"a:furnace"_s));
        QCOMPARE(block.soundMaterial, u"a:metal"_s);
        QCOMPARE(block.soundKeys, furnace.soundKeys);
        QCOMPARE(block.machine.sound, u"a:roar"_s);
        const QcMobDefinition &bee = copy.content->mobs().constFirst();
        QCOMPARE(bee.ambientSound, u"a:buzz"_s);
        QCOMPARE(bee.hurtSound, u"a:ouch"_s);
        QCOMPARE(bee.deathSound, u"a:gone"_s);
        QCOMPARE(copy.content->sounds().size(), 1);
        QVERIFY(*copy.content->sound(u"a:buzz"_s) == buzz);

        // A client must not trust the server's synthesis parameters: they decide its work and memory.
        QcSoundDefinition huge = buzz;
        huge.name = u"a:huge"_s;
        huge.variants = QcSoundDefinition::maxVariants + 1;
        content->registerSound(huge);
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::ContentSync{content}), QcProtocol::Peer::Server).has_value());
        huge.variants = 1;
        huge.layers[0].duration = QcSoundDefinition::maxLayerSeconds + 1.0;
        content->registerSound(huge);
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::ContentSync{content}), QcProtocol::Peer::Server).has_value());
        huge.layers[0].duration = 1.0;
        huge.layers[0].frequency = std::numeric_limits<double>::quiet_NaN();
        content->registerSound(huge);
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::ContentSync{content}), QcProtocol::Peer::Server).has_value());
        huge.layers[0].frequency = 220.0;
        content->registerSound(huge);
        QVERIFY(QcProtocol::decode(QcProtocol::encode(QcMsg::ContentSync{content}), QcProtocol::Peer::Server).has_value());
    }

    void craftingMessagesRoundTrip()
    {
        const QcMsg::MoveInventoryItem move = roundTrip(QcMsg::MoveInventoryItem{3, 40, 7});
        QCOMPARE(move.from, 3);
        QCOMPARE(move.to, 40);
        QCOMPARE(move.count, 7);
        const QcMsg::CraftRecipe craft = roundTrip(QcMsg::CraftRecipe{u"qtcraft:stick"_s, 5});
        QCOMPARE(craft.recipe, u"qtcraft:stick"_s);
        QCOMPARE(craft.count, 5);
        QCOMPARE(roundTrip(QcMsg::OpenCrafting{3}).size, 3);
        roundTrip(QcMsg::TakeCraftingResult{});
        roundTrip(QcMsg::CloseCrafting{});
        roundTrip(QcMsg::ConsumeItem{});

        QcMsg::InventoryState state;
        state.stacks = QList<QcItemStack>(36);
        state.stacks[4] = {2, 10, 0};
        state.selected = 4;
        state.craftingGrid = QList<QcItemStack>(9);
        state.craftingGrid[8] = {5, 1, 3};
        state.craftingSize = 3;
        state.craftingResult = {7, 4, 0};
        const QcMsg::InventoryState copy = roundTrip(state);
        QCOMPARE(copy.stacks, state.stacks);
        QCOMPARE(copy.selected, 4);
        QCOMPARE(copy.craftingGrid, state.craftingGrid);
        QCOMPARE(copy.craftingSize, 3);
        QCOMPARE(copy.craftingResult, state.craftingResult);
    }

    void machineAndFoodMessagesRoundTrip()
    {
        const QcMsg::OpenMachine open = roundTrip(QcMsg::OpenMachine{{-7, 64, 12}, u"smelting"_s});
        QCOMPARE(open.pos, (QcBlockPos{-7, 64, 12}));
        QCOMPARE(open.category, u"smelting"_s);

        QcMsg::MachineState state;
        state.pos = {3, 70, -2};
        state.stacks = {{4, 7, 0}, {9, 2, 0}, {11, 64, 0}};
        state.burn = 0.25f;
        state.progress = 0.75f;
        const QcMsg::MachineState copy = roundTrip(state);
        QCOMPARE(copy.pos, state.pos);
        QCOMPARE(copy.stacks, state.stacks);
        QCOMPARE(copy.burn, 0.25f);
        QCOMPARE(copy.progress, 0.75f);

        roundTrip(QcMsg::CloseMachine{});
        const QcMsg::PlayerFood food = roundTrip(QcMsg::PlayerFood{13, 20});
        QCOMPARE(food.food, 13);
        QCOMPARE(food.maxFood, 20);

        const QcMsg::MoveInventoryItem move = roundTrip(QcMsg::MoveInventoryItem{QcMsg::MoveInventoryItem::machineOutputSlot, 0, 0});
        QCOMPARE(move.from, 47);
    }

    void rejectsBadMachineAndFoodData()
    {
        // A machine state needs exactly three stacks.
        QcMsg::MachineState twoSlots;
        twoSlots.stacks = {{1, 1, 0}, {1, 1, 0}};
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(twoSlots), QcProtocol::Peer::Server).has_value());
        QcMsg::MachineState nan;
        nan.stacks = QList<QcItemStack>(3);
        nan.burn = std::numeric_limits<float>::quiet_NaN();
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(nan), QcProtocol::Peer::Server).has_value());
        // Out-of-range shares are clamped.
        QcMsg::MachineState large;
        large.stacks = QList<QcItemStack>(3);
        large.progress = 7.0f;
        large.burn = -1.0f;
        const QcMsg::MachineState clamped = roundTrip(large);
        QCOMPARE(clamped.progress, 1.0f);
        QCOMPARE(clamped.burn, 0.0f);
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::PlayerFood{21, 20}), QcProtocol::Peer::Server).has_value());
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::PlayerFood{-1, 20}), QcProtocol::Peer::Server).has_value());
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::OpenMachine{{}, QString(65, u'a')}), QcProtocol::Peer::Server)
                     .has_value());

        // Clients may only close machines; opening, state and food come from the server.
        for (const QcMessage &message : {QcMessage(QcMsg::OpenMachine{}), QcMessage(nan), QcMessage(QcMsg::PlayerFood{1, 20})}) {
            QVERIFY(!QcProtocol::decode(QcProtocol::encode(message), QcProtocol::Peer::Client).has_value());
        }
        const QByteArray close = QcProtocol::encode(QcMsg::CloseMachine{});
        QVERIFY(QcProtocol::decode(close, QcProtocol::Peer::Client).has_value());
        QVERIFY(QcProtocol::decode(close, QcProtocol::Peer::Server).has_value());
    }

    void appearancesRoundTripAndAreBounded()
    {
        QcAppearance look;
        look.outfit = u"mod:robe"_s;
        look.hair.clear();
        look.skinColor = 0x123456;
        look.pantsColor = 0xFFFFFFFF;
        const QcMsg::Hello hello = roundTrip(QcMsg::Hello{QcProtocol::version, u"Ann"_s, look});
        QCOMPARE(hello.appearance.outfit, u"mod:robe"_s);
        QVERIFY(hello.appearance.hair.isEmpty());
        QCOMPARE(hello.appearance.skinColor, 0x123456u);
        // Only the RGB bits survive.
        QCOMPARE(hello.appearance.pantsColor, 0xFFFFFFu);
        QCOMPARE(roundTrip(QcMsg::SetAppearance{look}).appearance.outfit, u"mod:robe"_s);
        QcMsg::EntitySpawn spawn;
        spawn.appearance = look;
        QCOMPARE(roundTrip(spawn).appearance.outfit, u"mod:robe"_s);
        // Overlong names are refused.
        look.outfit = QString(QcAppearance::maxNameLength + 1, u'x');
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::SetAppearance{look}), QcProtocol::Peer::Client).has_value());
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::Hello{QcProtocol::version, u"Ann"_s, look}), QcProtocol::Peer::Client)
                     .has_value());
        // A Hello of another version is still read far enough to report the mismatch.
        const QcResult<QcMessage> old = QcProtocol::decode(QcProtocol::encode(QcMsg::Hello{QcProtocol::version - 1, u"Old"_s, look}),
                                                           QcProtocol::Peer::Client);
        QVERIFY(old.has_value());
        QCOMPARE(std::get<QcMsg::Hello>(*old).protocolVersion, QcProtocol::version - 1);
        // Only clients set their appearance.
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::SetAppearance{}), QcProtocol::Peer::Server).has_value());
        QcMsg::EntityMoves moves;
        moves.moves = {{1, {}, 0.0f, 0.0, false, std::numeric_limits<float>::quiet_NaN(), 0, qcNoItem}};
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(moves), QcProtocol::Peer::Server).has_value());
    }

    void rejectsMalformedFrames()
    {
        QVERIFY(!QcProtocol::decode(QByteArray(), QcProtocol::Peer::Server).has_value());
        QVERIFY(!QcProtocol::decode(frameWithTag(999, {}), QcProtocol::Peer::Server).has_value());

        const QByteArray hello = QcProtocol::encode(QcMsg::Hello{QcProtocol::version, u"player"_s, {}});
        QVERIFY(!QcProtocol::decode(hello.left(hello.size() - 3), QcProtocol::Peer::Client).has_value());
        QVERIFY(!QcProtocol::decode(hello + QByteArray(1, 'x'), QcProtocol::Peer::Client).has_value());

        // A truncated chunk and a palette size of zero (byte 13 is the low byte of section 0's size).
        const QByteArray chunkFrame = QcProtocol::encode(QcMsg::ChunkData{std::make_shared<QcChunk>()});
        QVERIFY(!QcProtocol::decode(chunkFrame.left(chunkFrame.size() - 1), QcProtocol::Peer::Server).has_value());
        QByteArray zeroPalette = chunkFrame;
        zeroPalette[13] = 0;
        QVERIFY(!QcProtocol::decode(zeroPalette, QcProtocol::Peer::Server).has_value());
    }

    void messagesAreOnlyAcceptedFromTheirSender()
    {
        const QByteArray chunk = QcProtocol::encode(QcMsg::ChunkData{std::make_shared<QcChunk>()});
        QVERIFY(QcProtocol::decode(chunk, QcProtocol::Peer::Server).has_value());
        QVERIFY(!QcProtocol::decode(chunk, QcProtocol::Peer::Client).has_value());
        const QByteArray input = QcProtocol::encode(QcMsg::PlayerInput{});
        QVERIFY(QcProtocol::decode(input, QcProtocol::Peer::Client).has_value());
        QVERIFY(!QcProtocol::decode(input, QcProtocol::Peer::Server).has_value());
        const QByteArray bye = QcProtocol::encode(QcMsg::Disconnect{u"x"_s});
        QVERIFY(QcProtocol::decode(bye, QcProtocol::Peer::Client).has_value());
        QVERIFY(QcProtocol::decode(bye, QcProtocol::Peer::Server).has_value());
    }

    void runLengthDataCannotOverflowASection()
    {
        // Two runs of 4000 blocks claim 8000 entries in a section of 4096.
        QByteArray raw;
        {
            QDataStream stream(&raw, QIODevice::WriteOnly);
            stream.setVersion(QDataStream::Qt_6_8);
            stream << qint32(0) << qint32(0);
            stream << quint32(2) << QcBlockId(0) << QcBlockId(1);
            stream << quint32(2) << quint16(4000) << quint16(0) << quint16(4000) << quint16(1);
        }
        QDataStream in(raw);
        in.setVersion(QDataStream::Qt_6_8);
        QVERIFY(!QcProtocol::readChunk(in).has_value());
    }

    void runLengthDataMustFillTheSection()
    {
        QByteArray raw;
        {
            QDataStream stream(&raw, QIODevice::WriteOnly);
            stream.setVersion(QDataStream::Qt_6_8);
            stream << qint32(0) << qint32(0);
            stream << quint32(2) << QcBlockId(0) << QcBlockId(1);
            stream << quint32(1) << quint16(100) << quint16(1);
        }
        QDataStream in(raw);
        in.setVersion(QDataStream::Qt_6_8);
        QVERIFY(!QcProtocol::readChunk(in).has_value());
    }

    void terrainChunksEncodeCompactly()
    {
        // 64 layers of alternating stone and dirt: 4 sections with long runs, 12 empty sections.
        auto chunk = std::make_shared<QcChunk>();
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 16; ++x) {
                for (int z = 0; z < 16; ++z) {
                    chunk->setBlock(x, y, z, QcBlockId(1 + y % 2));
                }
            }
        }
        const QByteArray frame = QcProtocol::encode(QcMsg::ChunkData{chunk});
        // Each section: palette (4 + 3 * 4 bytes) + 16 runs of 256 (4 + 16 * 4 bytes). Raw indices would be 8 kB.
        QVERIFY2(frame.size() < 1000, qPrintable(QString::number(frame.size())));
    }

    void rejectsNonFiniteMovement()
    {
        QcPlayerInput input;
        input.forward = std::numeric_limits<float>::quiet_NaN();
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::PlayerInput{input}), QcProtocol::Peer::Client).has_value());
        input.forward = 0.0f;
        input.yaw = std::numeric_limits<float>::infinity();
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::PlayerInput{input}), QcProtocol::Peer::Client).has_value());
        QcMsg::PlayerState state;
        state.motion.position.y = std::numeric_limits<double>::quiet_NaN();
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(state), QcProtocol::Peer::Server).has_value());
    }

    void entityAndStatusMessagesRoundTrip()
    {
        const QcMsg::EntitySpawn spawn = roundTrip(QcMsg::EntitySpawn{42, 3, u"Alex"_s, {1.5, 70.0, -2.25}, 90.0f, 17.5, {}, {}, 12.5f, QcMsg::Swing | QcMsg::OnGround, 5});
        QCOMPARE(spawn.id, 42u);
        QCOMPARE(spawn.mobType, 3);
        QCOMPARE(spawn.name, u"Alex"_s);
        QCOMPARE(spawn.position, (QcVec3{1.5, 70.0, -2.25}));
        QCOMPARE(spawn.yaw, 90.0f);
        QCOMPARE(spawn.health, 17.5);

        QcMsg::EntityMoves moves;
        moves.moves = {{1, {0.0, 64.0, 0.0}, 10.0f, 5.0, true, -30.0f, QcMsg::InWater, 3}, {2, {3.0, 65.0, 4.0}, -45.0f, 9.0, false, 0.0f, 0, qcNoItem}};
        const QcMsg::EntityMoves copy = roundTrip(moves);
        QCOMPARE(copy.moves.size(), 2);
        QCOMPARE(copy.moves.at(0).hurt, true);
        QCOMPARE(copy.moves.at(1).position, (QcVec3{3.0, 65.0, 4.0}));

        QCOMPARE(roundTrip(QcMsg::EntityRemove{77, false}).id, 77u);
        const QcMsg::PlayerHealth health = roundTrip(QcMsg::PlayerHealth{3.0, 20.0, true});
        QCOMPARE(health.health, 3.0);
        QCOMPARE(health.dead, true);
        QCOMPARE(roundTrip(QcMsg::WorldTime{1234.5, 600.0}).time, 1234.5);
        QCOMPARE(roundTrip(QcMsg::AttackEntity{9}).id, 9u);
        roundTrip(QcMsg::Respawn{});

        const QcMsg::EntitySpawn item = roundTrip(
            QcMsg::EntitySpawn{7, QcMsg::EntitySpawn::itemType, QString(), {0.5, 64.0, 0.5}, 0.0f, 0.0, {12, 33, 4}, {}, 0.0f, 0, qcNoItem});
        QCOMPARE(item.mobType, QcMsg::EntitySpawn::itemType);
        QCOMPARE(item.item, (QcItemStack{12, 33, 4}));
        QCOMPARE(roundTrip(QcMsg::DropItem{true}).wholeStack, true);
        QCOMPARE(roundTrip(QcMsg::DropItem{false}).wholeStack, false);
        QCOMPARE(spawn.pitch, 12.5f);
        QCOMPARE(spawn.flags, quint8(QcMsg::Swing | QcMsg::OnGround));
        QCOMPARE(spawn.held, 5u);
        QCOMPARE(copy.moves.at(0).pitch, -30.0f);
        QCOMPARE(copy.moves.at(0).flags, quint8(QcMsg::InWater));
        QCOMPARE(copy.moves.at(0).held, 3u);
        QCOMPARE(roundTrip(QcMsg::EntityRemove{8, true}).died, true);
        // Dropping is a client request; a server must not send it.
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::DropItem{true}), QcProtocol::Peer::Server).has_value());
    }

    void rejectsBadEntityAndTimeData()
    {
        QcMsg::EntitySpawn spawn;
        spawn.position.x = std::numeric_limits<double>::infinity();
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(spawn), QcProtocol::Peer::Server).has_value());
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::WorldTime{0.0, 0.0}), QcProtocol::Peer::Server).has_value());
        // Combat messages only come from clients.
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::AttackEntity{1}), QcProtocol::Peer::Server).has_value());
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMsg::PlayerHealth{}), QcProtocol::Peer::Client).has_value());
    }

    void rejectsPaletteIndexOutsidePalette()
    {
        QByteArray raw;
        {
            QDataStream stream(&raw, QIODevice::WriteOnly);
            stream.setVersion(QDataStream::Qt_6_8);
            stream << qint32(0) << qint32(0);
            // Section 0: palette of two entries but an index pointing at entry 5.
            stream << quint32(2) << QcBlockId(0) << QcBlockId(1);
            for (int i = 0; i < QcWorld::blocksPerSection; ++i) {
                stream << quint16(i == 100 ? 5 : 0);
            }
        }
        QDataStream in(raw);
        in.setVersion(QDataStream::Qt_6_8);
        QVERIFY(!QcProtocol::readChunk(in).has_value());
    }

    void rejectsOversizedPalette()
    {
        QByteArray raw;
        {
            QDataStream stream(&raw, QIODevice::WriteOnly);
            stream.setVersion(QDataStream::Qt_6_8);
            stream << qint32(0) << qint32(0) << quint32(0x7FFFFFFF);
        }
        QDataStream in(raw);
        in.setVersion(QDataStream::Qt_6_8);
        QVERIFY(!QcProtocol::readChunk(in).has_value());
    }
};

QTEST_GUILESS_MAIN(TestProtocol)
#include "test_protocol.moc"
