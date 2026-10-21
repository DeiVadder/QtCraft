#include "common/protocol/QcProtocol.h"

#include <QDataStream>
#include <QIODevice>
#include <QTest>

// The messages of the creative mode (decisions E-282 to E-285): the game mode, creative items and
// pick block, their round trips, their senders and the checks of their values; the adventure
// allow-lists that travel with the mode (decision E-735); the spectator's flight speed and mark in
// the player list (decision E-730).
using namespace Qt::StringLiterals;

namespace {

template<typename T>
QcResult<QcMessage> decodeFrom(const T &message, QcProtocol::Peer sender)
{
    return QcProtocol::decode(QcProtocol::encode(QcMessage(message)), sender);
}

QByteArray gameModeFrame(quint8 value)
{
    QByteArray frame;
    QDataStream stream(&frame, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_8);
    // Two empty adventure allow-lists follow the mode (decision E-735).
    stream << quint16(QcMessage(QcMsg::GameMode{}).index()) << value << quint32(0) << quint32(0);
    return frame;
}

} // namespace

class TestGameModeProtocol : public QObject
{
    Q_OBJECT

private slots:
    void gameModeRoundTripsFromTheServerOnly()
    {
        const QcResult<QcMessage> decoded = decodeFrom(QcMsg::GameMode{QcGameMode::Creative, {}}, QcProtocol::Peer::Server);
        QVERIFY(decoded);
        QCOMPARE(std::get<QcMsg::GameMode>(*decoded).mode, QcGameMode::Creative);

        QVERIFY(!decodeFrom(QcMsg::GameMode{QcGameMode::Creative, {}}, QcProtocol::Peer::Client));
    }

    void unknownGameModesAreRejected()
    {
        QVERIFY(QcProtocol::decode(gameModeFrame(1), QcProtocol::Peer::Server));
        QVERIFY(QcProtocol::decode(gameModeFrame(2), QcProtocol::Peer::Server));
        QVERIFY(QcProtocol::decode(gameModeFrame(3), QcProtocol::Peer::Server));
        QVERIFY(!QcProtocol::decode(gameModeFrame(4), QcProtocol::Peer::Server));
        QVERIFY(!QcProtocol::decode(gameModeFrame(255), QcProtocol::Peer::Server));
    }

    void adventureAllowListsRoundTripWithTheMode()
    {
        QcAdventureRules rules;
        QVERIFY(rules.allow(QcAdventureRules::Action::Break, u"qtcraft:iron_pickaxe"_s, {u"qtcraft:stone"_s, u"qtcraft:glass"_s}));
        QVERIFY(rules.allow(QcAdventureRules::Action::PlaceOn, u"*"_s, {u"qtcraft:oak_planks"_s}));
        const QcResult<QcMessage> decoded = decodeFrom(QcMsg::GameMode{QcGameMode::Adventure, rules}, QcProtocol::Peer::Server);
        QVERIFY(decoded);
        QCOMPARE(std::get<QcMsg::GameMode>(*decoded).mode, QcGameMode::Adventure);
        QVERIFY(std::get<QcMsg::GameMode>(*decoded).adventure == rules);
    }

    void adventureAllowListsBeyondTheLimitsAreRejected()
    {
        QByteArray frame;
        QDataStream stream(&frame, QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_6_8);
        stream << quint16(QcMessage(QcMsg::GameMode{}).index()) << quint8(2) << quint32(QcAdventureRules::maxItems + 1);
        QVERIFY(!QcProtocol::decode(frame, QcProtocol::Peer::Server));

        // A name longer than the limit.
        QByteArray named;
        QDataStream out(&named, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_8);
        out << quint16(QcMessage(QcMsg::GameMode{}).index()) << quint8(2) << quint32(1) << QString(QcAdventureRules::maxNameLength + 1, u'x')
            << quint32(1) << u"qtcraft:stone"_s << quint32(0);
        QVERIFY(!QcProtocol::decode(named, QcProtocol::Peer::Server));
    }

    void spectatorFlightSpeedTravelsWithTheInputAndIsChecked()
    {
        QcPlayerInput input;
        input.flySpeed = 25;
        const QcResult<QcMessage> decoded = decodeFrom(QcMsg::PlayerInput{input}, QcProtocol::Peer::Client);
        QVERIFY(decoded);
        QCOMPARE(std::get<QcMsg::PlayerInput>(*decoded).input.flySpeed, quint8(25));

        input.flySpeed = 0;
        QVERIFY(!decodeFrom(QcMsg::PlayerInput{input}, QcProtocol::Peer::Client));
        input.flySpeed = QcPlayerInput::maxFlySpeed + 1;
        QVERIFY(!decodeFrom(QcMsg::PlayerInput{input}, QcProtocol::Peer::Client));
    }

    void playerListMarksSpectators()
    {
        QcMsg::PlayerList list;
        list.players = {{1, u"Alice"_s, true, false}, {2, u"Bob"_s, false, true}};
        const QcResult<QcMessage> decoded = decodeFrom(list, QcProtocol::Peer::Server);
        QVERIFY(decoded);
        QCOMPARE(std::get<QcMsg::PlayerList>(*decoded).players, list.players);
    }

    void creativeItemsRoundTripFromTheClientOnly()
    {
        const QcMsg::CreativeItem sent{7, {12, 64, 0}};
        const QcResult<QcMessage> decoded = decodeFrom(sent, QcProtocol::Peer::Client);
        QVERIFY(decoded);
        const auto &item = std::get<QcMsg::CreativeItem>(*decoded);
        QCOMPARE(item.slot, 7);
        QCOMPARE(item.stack, (QcItemStack{12, 64, 0}));

        QVERIFY(!decodeFrom(sent, QcProtocol::Peer::Server));
    }

    void creativeItemSlotsAndStacksAreChecked()
    {
        const auto accepted = [](qint32 slot, const QcItemStack &stack) {
            return decodeFrom(QcMsg::CreativeItem{slot, stack}, QcProtocol::Peer::Client).has_value();
        };
        QVERIFY(accepted(QcMsg::CreativeItem::addSlot, {3, 1, 0}));
        QVERIFY(accepted(0, {}));
        QVERIFY(accepted(35, {3, 1, 0}));
        QVERIFY(accepted(100, {3, 1, 0}));
        QVERIFY(accepted(103, {3, 1, 0}));

        QVERIFY(!accepted(-2, {3, 1, 0}));
        QVERIFY(!accepted(36, {3, 1, 0}));
        QVERIFY(!accepted(99, {3, 1, 0}));
        QVERIFY(!accepted(104, {3, 1, 0}));
        QVERIFY(!accepted(0, {3, -1, 0}));
        QVERIFY(!accepted(0, {3, 1000, 0}));
        QVERIFY(!accepted(0, {3, 1, -5}));
    }

    void pickBlockRoundTripsFromTheClientOnly()
    {
        const QcResult<QcMessage> decoded = decodeFrom(QcMsg::PickBlock{{-4, 70, 123}}, QcProtocol::Peer::Client);
        QVERIFY(decoded);
        QCOMPARE(std::get<QcMsg::PickBlock>(*decoded).pos, (QcBlockPos{-4, 70, 123}));
        QVERIFY(!decodeFrom(QcMsg::PickBlock{}, QcProtocol::Peer::Server));
    }
};

QTEST_GUILESS_MAIN(TestGameModeProtocol)
#include "test_game_mode_protocol.moc"
