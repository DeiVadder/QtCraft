// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/gameplay/QcInventory.h"
#include "common/protocol/QcProtocol.h"
#include "common/protocol/QcProtocolCodec.h"
#include "fixtures/QcTestUtil.h"

#include <QDataStream>
#include <QRegularExpression>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;

// The limits of item data (decisions E-823, E-864, E-866): stacks beyond them are never made, the
// largest stack that keeps them fits, as often as each message carries stacks, into a frame, and a
// client cannot send contents.
class TestItemDataLimits : public QObject
{
    Q_OBJECT

    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    QcItemId item(const char *name) const { return m_content->itemId(QString::fromLatin1(name)).value_or(qcNoItem); }

    QcItemStack holding(QList<QcItemStack> contents) const { return QcItemStack{item("qtcraft:stick"), 1, 0}.withContents(std::move(contents)); }

    // A stack with the largest data of its own: a map id, the longest name, the most
    // enchantments with the longest names and the most anvil uses.
    QcItemStack loaded() const
    {
        const QcItemStack plain{item("qtcraft:dirt"), 64, 0};
        QList<QcEnchantment> enchantments;
        for (qsizetype i = 0; i < QcItemData::maxEnchantments; ++i) {
            const QString name = u"%1:"_s.arg(i, 2, 10, QChar(u'0')) + QString(QcItemData::maxEnchantmentName - 3, QChar(0x00C4));
            enchantments.append(QcEnchantment{name, QcItemData::maxEnchantmentLevel});
        }
        return plain.withMapId(0xFFFF'FFFFu)
            .withName(QString(QcItemData::maxNameLength, QChar(0x00C4)))
            .withEnchantments(enchantments)
            .withPriorWork(QcItemData::maxPriorWork);
    }

    // The largest stack the limits allow, every stack in it loaded: its first slot nests
    // maxDepth levels, the next ones hold one stack each until maxStacksInside, the rest of the
    // maxContents slots one stack.
    QcItemStack largest() const
    {
        const QString name = loaded().name();
        QcItemStack chain = loaded();
        for (int level = 1; level < QcItemData::maxDepth; ++level) {
            chain = holding({chain}).withMapId(1).withName(name);
        }
        QList<QcItemStack> outerSlots{chain};
        const int holdingOne = QcItemData::maxStacksInside - static_cast<int>(QcItemData::maxContents) - (QcItemData::maxDepth - 1);
        for (int slot = 0; slot < holdingOne; ++slot) {
            outerSlots.append(holding({loaded()}).withMapId(1).withName(name));
        }
        while (outerSlots.size() < QcItemData::maxContents) {
            outerSlots.append(loaded());
        }
        return holding(outerSlots).withMapId(0xFFFF'FFFFu).withName(name);
    }

    static qsizetype wireSize(const QcItemStack &stack)
    {
        QByteArray bytes;
        QDataStream out(&bytes, QIODevice::WriteOnly);
        QcProtocolCodec::writeStack(out, stack);
        return bytes.size();
    }

    // Returns \a message read back from its frame, which must fit below QcProtocol::maxFrameBytes.
    static QcResult<QcMessage> roundTrip(const QcMessage &message)
    {
        const QByteArray frame = QcProtocol::encode(message);
        if (frame.size() > QcProtocol::maxFrameBytes) {
            qWarning() << "frame of" << frame.size() << "bytes";
            return qcError(u"frame too large"_s);
        }
        return QcProtocol::decode(frame, QcProtocol::Peer::Server);
    }

private slots:
    void stacksBeyondTheLimitsAreNeverMade()
    {
        const QcItemStack dirt{item("qtcraft:dirt"), 1, 0};
        const QcItemStack named = dirt.withName(u"Soil"_s);
        // Refused data leaves the stack as it was, with a warning.
        const QRegularExpression refused(u"^Refused item data beyond its limits"_s);
        QTest::ignoreMessage(QtWarningMsg, refused);
        QCOMPARE(named.withName(QString(QcItemData::maxNameLength + 1, u'n')), named);
        QTest::ignoreMessage(QtWarningMsg, refused);
        QVERIFY(!dirt.withContents(QList<QcItemStack>(QcItemData::maxContents + 1, dirt)).data);
        QVERIFY(dirt.withContents(QList<QcItemStack>(QcItemData::maxContents, dirt)).hasContents());

        QcItemStack deep = dirt;
        for (int level = 0; level < QcItemData::maxDepth; ++level) {
            deep = holding({deep});
        }
        QCOMPARE(deep.itemData().depth(), QcItemData::maxDepth);
        QTest::ignoreMessage(QtWarningMsg, refused);
        QVERIFY(!holding({deep}).data);

        // A slot holding six stacks counts seven: nine of them are 63 stacks inside, ten are 70,
        // beyond maxStacksInside.
        const QcItemStack seven = holding(QList<QcItemStack>(6, dirt));
        QCOMPARE(holding(QList<QcItemStack>(9, seven)).itemData().stacksInside(), 63);
        QTest::ignoreMessage(QtWarningMsg, refused);
        QVERIFY(!holding(QList<QcItemStack>(10, seven)).data);
    }

    void theLargestStackFitsEveryMessage()
    {
        const QcItemStack stack = largest();
        QCOMPARE(stack.itemData().depth(), QcItemData::maxDepth);
        QCOMPARE(stack.itemData().stacksInside(), QcItemData::maxStacksInside);
        const qsizetype size = wireSize(stack);
        QVERIFY2(size < qsizetype(160) * 1024, qPrintable(QString::number(size)));

        // A player's inventory full of them, with crafting grid, result and armour, fits into a
        // frame and comes back whole.
        QcMsg::InventoryState state;
        state.stacks = QList<QcItemStack>(QcInventory::slotCount, stack);
        state.craftingGrid = QList<QcItemStack>(QcMsg::InventoryState::craftingCells, stack);
        state.craftingSize = 3;
        state.craftingResult = stack;
        state.equipment.stacks.fill(stack);
        const QcResult<QcMessage> read = roundTrip(state);
        QVERIFY(read);
        QCOMPARE(std::get<QcMsg::InventoryState>(*read).stacks.constLast(), stack);

        // An entity with the stack and four armour stacks.
        QcMsg::EntitySpawn spawn;
        spawn.kind = QcEntityKind::ArmourStand;
        spawn.item = stack;
        spawn.equipment.stacks.fill(stack);
        QVERIFY(roundTrip(spawn));

        // The largest storage block.
        QcMsg::MachineState chest;
        chest.stacks = QList<QcItemStack>(QcMsg::MachineState::maxSlots, stack);
        QVERIFY(roundTrip(chest));

        // The largest trade screen, the longest list of stacks a message carries.
        QcMsg::TradeState trades;
        trades.offers = QList<QcMsg::TradeOffer>(QcMsg::TradeState::maxOffers,
                                                 QcMsg::TradeOffer{QList<QcItemStack>(QcMsg::TradeOffer::maxCosts, stack), stack, 0, 1});
        QVERIFY(roundTrip(trades));
    }

    void listsLongerThanTheirMessageAreRefused()
    {
        const QcItemStack dirt{item("qtcraft:dirt"), 1, 0};
        QcMsg::InventoryState state;
        state.stacks = QList<QcItemStack>(QcInventory::slotCount + 1, dirt);
        state.craftingGrid = QList<QcItemStack>(QcMsg::InventoryState::craftingCells, dirt);
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(state), QcProtocol::Peer::Server));
        state.stacks.removeLast();
        QVERIFY(QcProtocol::decode(QcProtocol::encode(state), QcProtocol::Peer::Server));
        state.craftingGrid.append(dirt);
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(state), QcProtocol::Peer::Server));

        QcMsg::TradeState trades;
        trades.offers = {QcMsg::TradeOffer{QList<QcItemStack>(QcMsg::TradeOffer::maxCosts + 1, dirt), dirt, 0, 1}};
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(trades), QcProtocol::Peer::Server));
    }

    void clientsCannotSendContents()
    {
        const QcItemStack dirt{item("qtcraft:dirt"), 1, 0};
        const auto fromClient = [](const QcItemStack &stack) {
            return QcProtocol::decode(QcProtocol::encode(QcMsg::CreativeItem{0, stack}), QcProtocol::Peer::Client).has_value();
        };
        QVERIFY(fromClient(dirt));
        // A name or a map id still decode; the server keeps only a filled map's id
        // (test_item_data_paths::creativeRequestsKeepOnlyTheMapId).
        QVERIFY(fromClient(dirt.withName(u"Soil"_s).withMapId(3)));
        QVERIFY(!fromClient(holding({dirt})));
        QVERIFY(!fromClient(largest()));
    }
};

QTEST_GUILESS_MAIN(TestItemDataLimits)
#include "test_item_data_limits.moc"
