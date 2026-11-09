// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/registry/QcAdvancementDefinition.h"
#include "common/core/QcWorldConstants.h"
#include "common/gameplay/QcBeacon.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcProtocol.h"
#include "fixtures/QcTestUtil.h"
#include "machines/QcEnchantingFixture.h"
#include "server/machines/QcBeacons.h"
#include "server/storage/QcBlockEntityCodec.h"
#include "server/storage/QcLoadWarnings.h"

#include <QHash>
#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

// The beacon (decisions E-1142 to E-1144): the pyramid's level with mixed and broken layers, the
// beam with the sky, glass colours and bedrock, the effects' tiers, range and duration, the
// effects given to players within reach with the ticks driven by hand, the screen's payment and
// choice through the protocol (refusals warned), saving and loading of the chosen effects with
// the chunk leaving and coming back, the beam put out when the beacon breaks, no effects across
// dimensions, the advancements, and the protocol round trip and refusals of the new messages.
class TestBeacon : public QcEnchantingFixture
{
    Q_OBJECT

private:
    // A small world of blocks for the pure rules: air where nothing is set, nothing loaded above
    // `loadedTop`.
    struct Blocks
    {
        QHash<QcBlockPos, QcBlockId> cells;
        int loadedTop = QcWorld::worldHeight;

        QcBeacon::BlockAt blockAt() const
        {
            return [this](QcBlockPos pos) -> std::optional<QcBlockId> {
                if (pos.y >= loadedTop) {
                    return std::nullopt;
                }
                return cells.value(pos, qcAirBlockId);
            };
        }
    };

    static constexpr QcBlockPos origin{0, 64, 0};

    // Fills layer `n` (1 to 4) under the beacon at `beacon` with `blocks`, by turns.
    static void fillLayer(QHash<QcBlockPos, QcBlockId> &cells, QcBlockPos beacon, int n, const QList<QcBlockId> &blocks)
    {
        int turn = 0;
        for (int dx = -n; dx <= n; ++dx) {
            for (int dz = -n; dz <= n; ++dz) {
                cells.insert(beacon.offset(dx, -n, dz), blocks.at(turn++ % blocks.size()));
            }
        }
    }

    QList<QcBlockId> baseBlocks() const
    {
        return {id("qtcraft:iron_block"), id("qtcraft:gold_block"), id("qtcraft:emerald_block"), id("qtcraft:diamond_block"),
                id("qtcraft:netherite_block")};
    }

    // A beacon at tablePos() on a pyramid of `levels` layers of iron, open to the sky; returns it.
    QcBlockPos buildBeacon(int levels)
    {
        const QcBlockPos beacon = tablePos();
        for (int n = 1; n <= levels; ++n) {
            for (int dx = -n; dx <= n; ++dx) {
                for (int dz = -n; dz <= n; ++dz) {
                    putAt(beacon.offset(dx, -n, dz), id("qtcraft:iron_block"));
                }
            }
        }
        for (int y = beacon.y + 1; y < QcWorld::worldHeight; ++y) {
            m_server->world().setBlock({beacon.x, y, beacon.z}, qcAirBlockId);
        }
        putAt(beacon, id("qtcraft:beacon"));
        return beacon;
    }

    // Whether the player has the advancement `name` of the base content.
    bool done(const char *name)
    {
        const QcAdvancementDefinition *definition = m_content->advancements().find(u"qtcraft:"_s + QString::fromLatin1(name));
        return definition && player().advancements.progress.isDone(*definition);
    }

    QcBeacons::Entry &entry(QcBlockPos beacon) { return *m_server->world().beacons().at(beacon); }

    // Lets the beacons look once (every QcBeacon::checkIntervalTicks).
    void look() { ticks(QcBeacon::checkIntervalTicks); }

    // The open beacon's state as the client last heard it.
    const QcMsg::BeaconState &beaconState() const { return m_client->workstations.beaconState; }

    // Opens the beacon at `beacon` through the protocol.
    void openBeacon(QcBlockPos beacon)
    {
        send(QcMsg::UseItem{beacon, QcBlockFace::West});
        ticks(1);
    }

    // Puts `count` of `name` into the inventory and moves them onto the payment slot.
    void pay(const char *name, int count = 1)
    {
        player().inventory.setSlot(20, QcItemStack{item(name), count, 0});
        move(20, itemSlot);
    }

    template <typename Message>
    static std::optional<Message> roundTrip(const Message &message, QcProtocol::Peer sender)
    {
        const QcResult<QcMessage> decoded = QcProtocol::decode(QcProtocol::encode(QcMessage(message)), sender);
        if (!decoded || !std::holds_alternative<Message>(*decoded)) {
            return std::nullopt;
        }
        return std::get<Message>(*decoded);
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void theLevelCountsFullLayersOfAnyMix()
    {
        Blocks world;
        QCOMPARE(QcBeacon::levelAt(*m_content, origin, world.blockAt()), 0);
        for (int n = 1; n <= QcBeacon::maxLevel; ++n) {
            fillLayer(world.cells, origin, n, baseBlocks());
            QCOMPARE(QcBeacon::levelAt(*m_content, origin, world.blockAt()), n);
        }
        // A fifth layer adds nothing.
        fillLayer(world.cells, origin, 5, baseBlocks());
        QCOMPARE(QcBeacon::levelAt(*m_content, origin, world.blockAt()), QcBeacon::maxLevel);

        // A corner missing from layer 3 keeps the two above it; stone in layer 1 leaves nothing.
        world.cells.insert(origin.offset(3, -3, -3), id("qtcraft:stone"));
        QCOMPARE(QcBeacon::levelAt(*m_content, origin, world.blockAt()), 2);
        world.cells.remove(origin.offset(-1, -1, 1));
        QCOMPARE(QcBeacon::levelAt(*m_content, origin, world.blockAt()), 0);
    }

    void theBeamNeedsTheSkyAndTakesColours()
    {
        Blocks world;
        const int up = QcWorld::worldHeight - origin.y;
        QCOMPARE(QcBeacon::beamAt(*m_content, origin, world.blockAt()), (QList<QcBeamSection>{{QcBeacon::plainColour, up}}));

        // Clear glass passes, stone stops the beam, bedrock (the Nether's roof) does not.
        world.cells.insert(origin.offset(0, 3, 0), id("qtcraft:glass"));
        QCOMPARE(QcBeacon::beamAt(*m_content, origin, world.blockAt()).size(), 1);
        world.cells.insert(origin.offset(0, 9, 0), id("qtcraft:stone"));
        QVERIFY(QcBeacon::beamAt(*m_content, origin, world.blockAt()).isEmpty());
        world.cells.insert(origin.offset(0, 9, 0), id("qtcraft:bedrock"));
        QCOMPARE(QcBeacon::beamAt(*m_content, origin, world.blockAt()).size(), 1);

        // The first stained glass takes its own colour, the same colour again lengthens the
        // section, another one mixes half and half; a pane colours like its glass.
        world.cells.insert(origin.offset(0, 1, 0), id("qtcraft:red_stained_glass"));
        world.cells.insert(origin.offset(0, 2, 0), id("qtcraft:red_stained_glass_pane"));
        world.cells.insert(origin.offset(0, 4, 0), id("qtcraft:blue_stained_glass"));
        const QList<QcBeamSection> beam = QcBeacon::beamAt(*m_content, origin, world.blockAt());
        QCOMPARE(beam.size(), 3);
        QCOMPARE(beam.at(0), (QcBeamSection{QcBeacon::plainColour, 1}));
        QCOMPARE(beam.at(1), (QcBeamSection{0xB02E26, 3}));
        QCOMPARE(beam.at(2).colour, quint32{0x763968});
        int height = 0;
        for (const QcBeamSection &section : beam) {
            height += section.height;
        }
        QCOMPARE(height, up);

        // A column not loaded up to the top shines no beam yet.
        world.loadedTop = origin.y + 20;
        QVERIFY(QcBeacon::beamAt(*m_content, origin, world.blockAt()).isEmpty());
    }

    void theTiersRangesAndDurationsAreTheRoleModels()
    {
        QCOMPARE(QcBeacon::primaryEffects(1), (QStringList{u"qtcraft:speed"_s, u"qtcraft:haste"_s}));
        QCOMPARE(QcBeacon::primaryEffects(3).size(), 5);
        QCOMPARE(QcBeacon::rangeOf(1), 20);
        QCOMPARE(QcBeacon::rangeOf(4), 50);
        QCOMPARE(QcBeacon::durationTicksOf(1), 220);
        QCOMPARE(QcBeacon::durationTicksOf(4), 340);

        QVERIFY(QcBeacon::mayChoose(1, u"qtcraft:speed"_s, {}));
        QVERIFY(!QcBeacon::mayChoose(0, u"qtcraft:speed"_s, {}));
        QVERIFY(!QcBeacon::mayChoose(2, u"qtcraft:strength"_s, {}));
        QVERIFY(!QcBeacon::mayChoose(3, u"qtcraft:strength"_s, u"qtcraft:regeneration"_s));
        QVERIFY(QcBeacon::mayChoose(4, u"qtcraft:strength"_s, u"qtcraft:regeneration"_s));
        QVERIFY(QcBeacon::mayChoose(4, u"qtcraft:strength"_s, u"qtcraft:strength"_s));
        QVERIFY(!QcBeacon::mayChoose(4, u"qtcraft:strength"_s, u"qtcraft:speed"_s));
        QVERIFY(!QcBeacon::mayChoose(4, u"qtcraft:regeneration"_s, {}));

        // The same effect twice at level 4 is the primary one at level II.
        const QList<QcStatusEffect> twice = QcBeacon::effectsOf(4, u"qtcraft:haste"_s, u"qtcraft:haste"_s);
        QCOMPARE(twice.size(), 1);
        QCOMPARE(twice.first().level, 2);
        QCOMPARE(twice.first().ticksLeft, QcBeacon::durationTicksOf(4));
        QCOMPARE(QcBeacon::effectsOf(4, u"qtcraft:speed"_s, u"qtcraft:regeneration"_s).size(), 2);
        QVERIFY(QcBeacon::effectsOf(1, {}, {}).isEmpty());

        // Level 1 reaches 20 blocks across and any height above, 20 below; the player's box only
        // needs to touch that, so the head counts below and half the width at the sides.
        const QcBlockPos beacon{0, 64, 0};
        const auto body = [](double x, double y, double z) { return QcPhysics::boxAt({x, y, z}, QcPhysics::playerWidth, QcPhysics::playerHeight); };
        QVERIFY(QcBeacon::reaches(beacon, 1, body(21.2, 300.0, -19.5)));
        QVERIFY(!QcBeacon::reaches(beacon, 1, body(21.4, 64.0, 0.0)));
        QVERIFY(QcBeacon::reaches(beacon, 1, body(0.0, 42.3, 0.0)));
        QVERIFY(!QcBeacon::reaches(beacon, 1, body(0.0, 42.1, 0.0)));
    }

    void aShiningBeaconGivesItsEffectsWithinReach()
    {
        join();
        const QcBlockPos beacon = buildBeacon(1);
        look();
        QVERIFY(entry(beacon).isActive());
        QCOMPARE(entry(beacon).level, 1);
        QVERIFY(!m_client->workstations.beams.value(beacon).isEmpty());
        // Without a chosen effect it gives nothing.
        QVERIFY(!player().effects.find(u"qtcraft:speed"_s));

        entry(beacon).primary = u"qtcraft:speed"_s;
        look();
        const QcStatusEffect *speed = player().effects.find(u"qtcraft:speed"_s);
        QVERIFY(speed);
        QCOMPARE(speed->level, 1);
        QVERIFY(speed->ticksLeft > QcBeacon::durationTicksOf(1) - QcBeacon::checkIntervalTicks);

        // Out of reach the effect runs out instead of being renewed.
        player().effects.remove(u"qtcraft:speed"_s);
        player().motion.position = {beacon.x + 25.5, static_cast<double>(m_feet.y), beacon.z + 0.5};
        look();
        QVERIFY(!player().effects.find(u"qtcraft:speed"_s));

        // A roof puts the beam out: the client hears of it, and nobody gets the effect.
        player().motion.position = {m_feet.x + 0.5, static_cast<double>(m_feet.y), m_feet.z + 0.5};
        putAt(beacon.offset(0, 6, 0), id("qtcraft:stone"));
        look();
        QVERIFY(!entry(beacon).isActive());
        QVERIFY(m_client->workstations.beams.value(beacon).isEmpty());
        QVERIFY(!player().effects.find(u"qtcraft:speed"_s));
    }

    void theScreenTakesThePaymentForAllowedEffects()
    {
        join();
        const QcBlockPos beacon = buildBeacon(1);
        look();
        openBeacon(beacon);
        QCOMPARE(m_client->workstations.beacon, std::optional(beacon));
        QCOMPARE(beaconState().level, 1);

        // No payment: refused.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Beacon: choice of .* refused, no payment"_s));
        send(QcMsg::ChooseBeaconEffects{u"qtcraft:speed"_s, {}});
        QVERIFY(entry(beacon).primary.isEmpty());

        // Only one ingot goes onto the slot; strength needs level 3, regeneration level 4.
        pay("qtcraft:iron_ingot", 5);
        QCOMPARE(beaconState().payment.count, 1);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Beacon: choice of .* not allowed at level 1"_s));
        send(QcMsg::ChooseBeaconEffects{u"qtcraft:strength"_s, {}});
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Beacon: choice of .* not allowed at level 1"_s));
        send(QcMsg::ChooseBeaconEffects{u"qtcraft:speed"_s, u"qtcraft:regeneration"_s});
        QVERIFY(entry(beacon).primary.isEmpty());
        QCOMPARE(beaconState().payment.count, 1);

        send(QcMsg::ChooseBeaconEffects{u"qtcraft:haste"_s, {}});
        QCOMPARE(entry(beacon).primary, u"qtcraft:haste"_s);
        QVERIFY(beaconState().payment.isEmpty());
        QCOMPARE(beaconState().primary, u"qtcraft:haste"_s);
        QCOMPARE(player().inventory.countOf(item("qtcraft:iron_ingot")), 4);

        // Something else than a payment item stays off the slot.
        pay("qtcraft:stone");
        QVERIFY(beaconState().payment.isEmpty());
    }

    void closingGivesThePaymentBack()
    {
        join();
        const QcBlockPos beacon = buildBeacon(2);
        openBeacon(beacon);
        // The open screen hears of the level once the beacon has looked at its pyramid.
        QCOMPARE(beaconState().level, 0);
        look();
        QCOMPARE(beaconState().level, 2);
        pay("qtcraft:emerald");
        QCOMPARE(beaconState().payment.item, item("qtcraft:emerald"));
        QCOMPARE(player().inventory.countOf(item("qtcraft:emerald")), 0);

        send(QcMsg::CloseBeacon{});
        ticks(1);
        QVERIFY(!player().screen.beacon);
        QCOMPARE(player().inventory.countOf(item("qtcraft:emerald")), 1);
    }

    void theChosenEffectsAreSavedWithTheChunk()
    {
        const QcBlockPos pos{5, 70, 9};
        QcBlockEntityRecord record;
        record.beacons = {{pos, u"qtcraft:strength"_s, u"qtcraft:regeneration"_s}};
        QcLoadWarnings warnings;
        const QcResult<QcBlockEntityRecord> loaded = QcBlockEntityCodec::decode(QcBlockEntityCodec::encode(record, *m_content), pos.chunk(), *m_content, warnings);
        QVERIFY(loaded);
        QCOMPARE(loaded->beacons, record.beacons);

        // Through the world: a beacon whose chunk leaves and comes back keeps its effects.
        join();
        const QcBlockPos beacon = buildBeacon(1);
        entry(beacon).primary = u"qtcraft:speed"_s;
        const QList<QcBeacons::Saved> saved = m_server->world().beacons().savedOf(beacon.chunk());
        QCOMPARE(saved.size(), 1);
        QCOMPARE(saved.first().primary, u"qtcraft:speed"_s);

        // The chunk leaves: the beacon is forgotten. It comes back from its saved record.
        QcBlockEntityRecord stored;
        stored.beacons = saved;
        QcLoadWarnings reloadWarnings;
        const QcResult<QcBlockEntityRecord> reloaded
            = QcBlockEntityCodec::decode(QcBlockEntityCodec::encode(stored, *m_content), beacon.chunk(), *m_content, reloadWarnings);
        QVERIFY(reloaded);
        QcBeacons &beacons = m_server->world().beacons();
        beacons.forgetChunk(beacon.chunk());
        QVERIFY(!beacons.at(beacon));
        const std::shared_ptr<const QcChunk> chunk = m_server->world().chunk(beacon.chunk());
        QVERIFY(chunk);
        beacons.noteChunk(beacon.chunk(), *chunk);
        QVERIFY(beacons.at(beacon));
        QVERIFY(entry(beacon).primary.isEmpty());
        beacons.restore(reloaded->beacons);
        QCOMPARE(entry(beacon).primary, u"qtcraft:speed"_s);
    }

    void aBrokenBeaconPutsItsBeamOut()
    {
        join();
        const QcBlockPos beacon = buildBeacon(1);
        look();
        QVERIFY(!m_client->workstations.beams.value(beacon).isEmpty());
        // Broken, the client hears at once that the beam is gone.
        putAt(beacon, qcAirBlockId);
        ticks(1);
        QVERIFY(m_client->workstations.beams.value(beacon).isEmpty());
        QVERIFY(!m_server->world().beacons().at(beacon));
    }

    void aPlayerInAnotherDimensionGetsNothing()
    {
        join();
        const QcBlockPos beacon = buildBeacon(1);
        look();
        entry(beacon).primary = u"qtcraft:speed"_s;
        const auto hasSpeed = [this] {
            return std::ranges::any_of(m_client->effects, [](const QcStatusEffect &effect) { return effect.name == u"qtcraft:speed"_s; });
        };
        // At the same coordinates in the nether the overworld's beacon does not reach him (the
        // client hears of his effects).
        chat(u"/tp %1 %2 %3 in qtcraft:nether"_s.arg(beacon.x + 0.5).arg(beacon.y + 1).arg(beacon.z + 0.5));
        ticks(3);
        QCOMPARE(m_client->dimensions.last(), u"qtcraft:nether"_s);
        look();
        QVERIFY(!hasSpeed());
        // Back in the overworld it does.
        chat(u"/tp %1 %2 %3 in qtcraft:overworld"_s.arg(beacon.x + 0.5).arg(beacon.y + 1).arg(beacon.z + 0.5));
        ticks(3);
        look();
        QVERIFY(hasSpeed());
    }

    void aFullPyramidGrantsTheBeaconator()
    {
        join();
        buildBeacon(1);
        look();
        QVERIFY(done("beacon"));
        QVERIFY(!done("beaconator"));
        // The pyramid grows to four layers under the shining beacon.
        buildBeacon(4);
        look();
        QVERIFY(done("beaconator"));
    }

    void theMessagesSurviveTheProtocol()
    {
        const QcMsg::BeaconState state{{item("qtcraft:diamond"), 1, 0}, 3, u"qtcraft:strength"_s, {}};
        const std::optional<QcMsg::BeaconState> stateCopy = roundTrip(state, QcProtocol::Peer::Server);
        QVERIFY(stateCopy);
        QCOMPARE(stateCopy->payment, state.payment);
        QCOMPARE(stateCopy->level, 3);
        QCOMPARE(stateCopy->primary, state.primary);
        QVERIFY(stateCopy->secondary.isEmpty());

        const std::optional<QcMsg::OpenBeacon> open = roundTrip(QcMsg::OpenBeacon{{1, 2, 3}}, QcProtocol::Peer::Server);
        QVERIFY(open);
        QCOMPARE(open->pos, (QcBlockPos{1, 2, 3}));

        const QcMsg::ChooseBeaconEffects choice{u"qtcraft:haste"_s, u"qtcraft:haste"_s};
        const std::optional<QcMsg::ChooseBeaconEffects> choiceCopy = roundTrip(choice, QcProtocol::Peer::Client);
        QVERIFY(choiceCopy);
        QCOMPARE(choiceCopy->primary, choice.primary);
        QCOMPARE(choiceCopy->secondary, choice.secondary);
        // A client may not send the server's messages.
        QVERIFY(!roundTrip(choice, QcProtocol::Peer::Server));

        QVERIFY(roundTrip(QcMsg::CloseBeacon{}, QcProtocol::Peer::Client));
        QVERIFY(roundTrip(QcMsg::CloseBeacon{}, QcProtocol::Peer::Server));

        const QcMsg::BeaconBeam beam{{4, 5, 6}, {{QcBeacon::plainColour, 1}, {0xB02E26, 200}}};
        const std::optional<QcMsg::BeaconBeam> beamCopy = roundTrip(beam, QcProtocol::Peer::Server);
        QVERIFY(beamCopy);
        QCOMPARE(beamCopy->pos, beam.pos);
        QCOMPARE(beamCopy->sections, beam.sections);
        // A level beyond 4 is refused.
        QcMsg::BeaconState tooHigh = state;
        tooHigh.level = 5;
        QVERIFY(!roundTrip(tooHigh, QcProtocol::Peer::Server));
    }

    void implausibleMessagesAreRefused()
    {
        const QString longName(129, u'a');
        QVERIFY(!roundTrip(QcMsg::ChooseBeaconEffects{longName, {}}, QcProtocol::Peer::Client));
        QVERIFY(!roundTrip(QcMsg::ChooseBeaconEffects{{}, longName}, QcProtocol::Peer::Client));
        QVERIFY(!roundTrip(QcMsg::BeaconState{{}, 1, longName, {}}, QcProtocol::Peer::Server));
        QVERIFY(roundTrip(QcMsg::ChooseBeaconEffects{QString(128, u'a'), {}}, QcProtocol::Peer::Client));

        // A section without height, too many sections, a beam beyond the top, a beacon outside.
        QVERIFY(!roundTrip(QcMsg::BeaconBeam{{4, 5, 6}, {{QcBeacon::plainColour, 0}}}, QcProtocol::Peer::Server));
        const QcBeamSection one{QcBeacon::plainColour, 1};
        QVERIFY(!roundTrip(QcMsg::BeaconBeam{{4, 0, 6}, QList<QcBeamSection>(QcMsg::BeaconBeam::maxSections + 1, one)}, QcProtocol::Peer::Server));
        QVERIFY(!roundTrip(QcMsg::BeaconBeam{{4, 5, 6}, {{QcBeacon::plainColour, QcWorld::worldHeight - 4}}}, QcProtocol::Peer::Server));
        QVERIFY(roundTrip(QcMsg::BeaconBeam{{4, 5, 6}, {{QcBeacon::plainColour, QcWorld::worldHeight - 5}}}, QcProtocol::Peer::Server));
        QVERIFY(!roundTrip(QcMsg::BeaconBeam{{4, -1, 6}, {}}, QcProtocol::Peer::Server));
        QVERIFY(!roundTrip(QcMsg::BeaconBeam{{4, QcWorld::worldHeight, 6}, {}}, QcProtocol::Peer::Server));
    }

    void theBlockAndItsContentAreTheRoleModels()
    {
        const QcBlockDefinition &block = m_content->block(id("qtcraft:beacon"));
        QVERIFY(block.opensBeacon);
        QCOMPARE(block.lightLevel, 15);
        QCOMPARE(block.hardness, 3.0);
        for (const char *name : {"qtcraft:iron_ingot", "qtcraft:gold_ingot", "qtcraft:emerald", "qtcraft:diamond", "qtcraft:netherite_ingot"}) {
            QVERIFY2(QcBeacon::isPayment(*m_content, item(name)), name);
        }
        QVERIFY(!QcBeacon::isPayment(*m_content, item("qtcraft:copper_ingot")));
        QCOMPARE(m_content->block(id("qtcraft:white_stained_glass")).beamColour, std::optional<quint32>(0xF9FFFE));
        QVERIFY(!m_content->block(id("qtcraft:glass")).beamColour);
    }
};

QTEST_GUILESS_MAIN(TestBeacon)
#include "test_beacon.moc"
