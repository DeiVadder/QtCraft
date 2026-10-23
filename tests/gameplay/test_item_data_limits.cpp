#include "common/gameplay/QcInventory.h"
#include "common/protocol/QcProtocol.h"
#include "common/protocol/QcProtocolCodec.h"
#include "QcTestUtil.h"

#include <QDataStream>
#include <QRegularExpression>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;

// The limits of item data (decision E-823): stacks beyond them are never made, the largest stack
// that keeps them fits into every message that carries stacks, and a client cannot send contents.
class TestItemDataLimits : public QObject
{
    Q_OBJECT

    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    QcItemId item(const char *name) const { return m_content->itemId(QString::fromLatin1(name)).value_or(qcNoItem); }

    QcItemStack holding(QList<QcItemStack> contents) const { return QcItemStack{item("qtcraft:stick"), 1, 0}.withContents(std::move(contents)); }

    // A stack with the largest data of its own: a map id and the longest name.
    QcItemStack loaded() const
    {
        const QcItemStack plain{item("qtcraft:dirt"), 64, 0};
        return plain.withMapId(0xFFFF'FFFFu).withName(QString(QcItemData::maxNameLength, QChar(0x00C4)));
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
        QVERIFY2(size < qsizetype(10) * 1024, qPrintable(QString::number(size)));

        // A player's inventory full of them, with crafting grid, result and armour, stays below
        // 1 MiB and comes back whole.
        QcMsg::InventoryState state;
        state.stacks = QList<QcItemStack>(QcInventory::slotCount, stack);
        state.craftingGrid = QList<QcItemStack>(9, stack);
        state.craftingSize = 3;
        state.craftingResult = stack;
        state.equipment.stacks.fill(stack);
        const QByteArray frame = QcProtocol::encode(state);
        QVERIFY2(frame.size() < qsizetype(1024) * 1024, qPrintable(QString::number(frame.size())));
        const QcResult<QcMessage> read = QcProtocol::decode(frame, QcProtocol::Peer::Server);
        QVERIFY(read);
        QCOMPARE(std::get<QcMsg::InventoryState>(*read).stacks.constLast(), stack);

        // An entity with the stack and four armour stacks; the largest list a message may carry.
        QcMsg::EntitySpawn spawn;
        spawn.kind = QcEntityKind::ArmourStand;
        spawn.item = stack;
        spawn.equipment.stacks.fill(stack);
        const QByteArray spawnFrame = QcProtocol::encode(spawn);
        QVERIFY(spawnFrame.size() < qsizetype(64) * 1024);
        QVERIFY(QcProtocol::decode(spawnFrame, QcProtocol::Peer::Server));
        QVERIFY(1024 * size < QcProtocol::maxFrameBytes / 2);
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
