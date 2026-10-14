#include "QcProtocol.h"
#include "QcStreamLimits.h"

#include <QTest>

#include <limits>

using namespace Qt::StringLiterals;

namespace {

template<typename T>
T roundTrip(const T &message)
{
    const QcMessage wrapped(message);
    const QcResult<QcMessage> decoded = QcProtocol::decode(QcProtocol::encode(wrapped), QcProtocol::Peer::Server);
    if (!decoded || !std::holds_alternative<T>(*decoded)) {
        qFatal("decode failed");
    }
    return std::get<T>(*decoded);
}

} // namespace

// The entity messages of the protocol (stage 31.2, decision E-365): every kind with what it carries
// (a creature's type, the block of falling and sliding blocks, the armour and pose of stands and
// players), and unknown kinds or creature types refused by the decoder.
class TestEntityProtocol : public QObject
{
    Q_OBJECT

private slots:
    void entityKindsRoundTripAndUnknownOnesAreRejected()
    {
        // Every kind arrives as itself (decision E-365); only a creature carries its type.
        for (int value = 0; value < QcEntityKinds::count; ++value) {
            QcMsg::EntitySpawn spawn;
            spawn.id = 70 + quint32(value);
            spawn.kind = QcEntityKind(value);
            spawn.creatureType = 5;
            spawn.name = u"Kind"_s;
            const QcMsg::EntitySpawn copy = roundTrip(spawn);
            QCOMPARE(copy.kind, spawn.kind);
            QCOMPARE(copy.creatureType, spawn.kind == QcEntityKind::Creature ? 5 : 0);
            QCOMPARE(copy.name, u"Kind"_s);
        }

        // The kind is the byte after the id: [quint16 tag][quint32 id][quint8 kind]. A player's spawn
        // has no creature type after it, so the rest of the frame stays readable with any kind byte.
        constexpr int kindOffset = 6;
        QcMsg::EntitySpawn player;
        player.kind = QcEntityKind::Player;
        const QByteArray valid = QcProtocol::encode(player);
        QVERIFY(QcProtocol::decode(valid, QcProtocol::Peer::Server).has_value());
        QcMsg::EntitySpawn creature;
        creature.kind = QcEntityKind::Creature;
        creature.creatureType = 2;
        for (const quint8 bad : {quint8(QcEntityKinds::count), quint8(7), quint8(255)}) {
            QByteArray frame = valid;
            frame[kindOffset] = char(bad);
            const bool decoded = QcProtocol::decode(frame, QcProtocol::Peer::Server).has_value();
            QVERIFY2(!decoded, qPrintable(u"kind %1"_s.arg(bad)));
        }
        // Negative and absurd creature types are refused before anything of the entity is used.
        for (const qint32 type : {qint32(-1), qint32(QcStreamLimits::maxDefinitions), std::numeric_limits<qint32>::max()}) {
            QcMsg::EntitySpawn odd = creature;
            odd.creatureType = type;
            const bool decoded = QcProtocol::decode(QcProtocol::encode(odd), QcProtocol::Peer::Server).has_value();
            QVERIFY2(!decoded, qPrintable(u"creature type %1"_s.arg(type)));
        }
        QcMsg::EntitySpawn last = creature;
        last.creatureType = qint32(QcStreamLimits::maxDefinitions) - 1;
        QCOMPARE(roundTrip(last).creatureType, last.creatureType);
    }

    void equipmentOfOthersRoundTrips()
    {
        // Armour slots (stage 23) travel with a player's spawn, so others can draw them.
        QcMsg::EntitySpawn spawn;
        spawn.name = u"Ann"_s;
        spawn.equipment.at(QcEquipmentSlot::Head) = {3, 1, 0};
        spawn.equipment.at(QcEquipmentSlot::Feet) = {4, 1, 120};
        const QcMsg::EntitySpawn copy = roundTrip(spawn);
        QCOMPARE(copy.equipment, spawn.equipment);
        QCOMPARE(copy.equipment.at(QcEquipmentSlot::Feet).damage, 120);
        QCOMPARE(copy.name, u"Ann"_s);
        QCOMPARE(QcEquipment::slotCount, std::size_t(4));
    }

    void armourStandsRoundTrip()
    {
        // Stands (protocol 23) carry their pose and flags; other entities do not.
        QcMsg::EntitySpawn spawn;
        spawn.id = 5;
        spawn.kind = QcEntityKind::ArmourStand;
        spawn.name = u"Tin"_s;
        spawn.held = 12;
        spawn.equipment.at(QcEquipmentSlot::Legs) = {6, 1, 2};
        spawn.stand.at(QcStandPose::Part::RightArm) = {-90.0f, 5.0f, 370.0f};
        spawn.stand.small = true;
        spawn.stand.basePlate = false;
        const QcMsg::EntitySpawn copy = roundTrip(spawn);
        QCOMPARE(copy.equipment, spawn.equipment);
        QCOMPARE(copy.held, QcItemId(12));
        // Angles arrive normalised.
        QCOMPARE(copy.stand.at(QcStandPose::Part::RightArm), (QcStandPose::Rotation{-90.0f, 5.0f, 10.0f}));
        QVERIFY(copy.stand.small && !copy.stand.basePlate && copy.stand.arms);

        QcMsg::EntitySpawn player = spawn;
        player.kind = QcEntityKind::Player;
        QCOMPARE(roundTrip(player).stand, QcStandPose());
        QCOMPARE(QcProtocol::version, 32u);
    }

    void blockEntitiesCarryTheirBlock()
    {
        // Falling blocks carry their block state.
        QcMsg::EntitySpawn falling{11, QcEntityKind::FallingBlock, QString(), {0.5, 80.0, 0.5}, 0.0f, 0.0, {}, {}, 0.0f, 0, qcNoItem};
        falling.block = 37;
        const QcMsg::EntitySpawn fallingCopy = roundTrip(falling);
        QCOMPARE(fallingCopy.kind, QcEntityKind::FallingBlock);
        QCOMPARE(fallingCopy.block, 37u);
        // So do blocks a piston moves (decision E-337).
        QcMsg::EntitySpawn moving = falling;
        moving.kind = QcEntityKind::MovingBlock;
        moving.block = 4242;
        const QcMsg::EntitySpawn movingCopy = roundTrip(moving);
        QCOMPARE(movingCopy.kind, QcEntityKind::MovingBlock);
        QCOMPARE(movingCopy.block, 4242u);
        QCOMPARE(movingCopy.position, moving.position);
    }
};

QTEST_GUILESS_MAIN(TestEntityProtocol)
#include "test_entity_protocol.moc"
