#include "common/gameplay/QcInventory.h"
#include "common/gameplay/QcSlotList.h"
#include "common/maps/QcMapPixel.h"
#include "QcTestUtil.h"
#include "server/maps/QcMapPending.h"
#include "server/maps/QcMapSaveCodec.h"
#include "server/maps/QcMapStore.h"
#include "server/storage/QcSaveFile.h"

#include <QDataStream>
#include <QTest>
#include <QtEndian>

#include <memory>

using namespace Qt::StringLiterals;

// The map store without a server (decisions E-798, E-799, E-802): what waits for a holder, which
// maps leave memory, how map stacks merge and what a map file with unknown blocks reads as.
class TestMapStore : public QObject
{
    Q_OBJECT

    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    QcItemId item(const char *name) const { return m_content->itemId(QString::fromLatin1(name)).value_or(qcNoItem); }

private slots:
    void changingRowsDoNotHoldBackTheOthers()
    {
        QcMapPending pending = QcMapPending::whole();
        constexpr int budget = 4096;
        QList<bool> sent(QcMapData::size, false);
        int takes = 0;
        // The top row changes before every take, as while a holder reveals the land there.
        while (sent.contains(false) && takes < 2 * QcMapData::size) {
            pending.add({0, 0});
            pending.add({QcMapData::size - 1, 0});
            const QRect taken = pending.take(budget);
            QVERIFY(!taken.isEmpty());
            QVERIFY(qsizetype(taken.width()) * taken.height() <= budget);
            for (int row = taken.top(); row <= taken.bottom(); ++row) {
                sent[row] = true;
            }
            ++takes;
        }
        // A whole map of 128 rows of 128 pixels goes out in four takes, the changed top row aside.
        QVERIFY(!sent.contains(false));
        QVERIFY(takes <= 5);

        // Narrow changes share one rectangle; nothing waiting gives an empty one.
        QcMapPending few;
        few.add({5, 10});
        few.add({6, 11});
        QCOMPARE(few.take(budget), QRect(QPoint(5, 10), QPoint(6, 11)));
        QVERIFY(few.isEmpty());
        QVERIFY(few.take(budget).isEmpty());
    }

    void unusedMapsLeaveMemoryOnceWritten()
    {
        int loads = 0;
        QcMapStore store;
        store.open(
            [&loads](quint32) -> std::optional<QcMapData> {
                ++loads;
                return QcMapData::around(0, 0, 0);
            },
            1);
        const std::optional<quint32> id = store.create(QcMapData::around(0, 0, 0));
        QVERIFY(id);
        constexpr quint64 idle = QcMapStore::idleTicks;
        constexpr quint64 interval = QcMapStore::evictionInterval;

        // Changed and not written, or taken by a save that is not done or failed: kept.
        store.tick(idle);
        QCOMPARE(store.loadedCount(), 1);
        const QList<quint32> saving = store.takeUnsaved();
        store.tick(2 * idle);
        QCOMPARE(store.loadedCount(), 1);
        store.finishSave(saving, false);
        store.tick(3 * idle);
        QCOMPARE(store.loadedCount(), 1);

        // Written: kept while used within idleTicks, then dropped and read again when wanted.
        store.finishSave(store.takeUnsaved(), true);
        store.tick(4 * idle + 1);
        QVERIFY(store.find(*id));
        store.tick(5 * idle);
        QCOMPARE(store.loadedCount(), 1);
        store.tick(5 * idle + interval);
        QCOMPARE(store.loadedCount(), 0);
        QVERIFY(!store.loaded(*id));
        QCOMPARE(loads, 0);
        QVERIFY(store.find(*id));
        QCOMPARE(loads, 1);

        // Without a save to read from (no world directory) maps stay.
        QcMapStore memoryOnly;
        memoryOnly.open({}, 1);
        QVERIFY(memoryOnly.create(QcMapData::around(0, 0, 0)));
        memoryOnly.finishSave(memoryOnly.takeUnsaved(), true);
        memoryOnly.tick(2 * idle);
        QCOMPARE(memoryOnly.loadedCount(), 1);
    }

    void mapsOfDifferentIdsNeverMerge()
    {
        const QcContentRegistry &content = *m_content;
        const QcItemId map = item("qtcraft:filled_map");
        QVERIFY(map != qcNoItem);

        // Adding: onto the stack of the same id, else into a free slot.
        QcInventory inventory;
        inventory.setSlot(0, {map, 1, 7});
        QCOMPARE(inventory.add(map, 1, content, 8), 0);
        QCOMPARE(inventory.add(map, 2, content, 7), 0);
        const QcItemStack first = inventory.slot(0);
        const QcItemStack second = inventory.slot(1);
        QCOMPARE(first, (QcItemStack{map, 3, 7}));
        QCOMPARE(second, (QcItemStack{map, 1, 8}));

        // Moving onto another id swaps; onto the same id merges.
        inventory.move(1, 0, content);
        const QcItemStack swapped = inventory.slot(0);
        QCOMPARE(swapped, (QcItemStack{map, 1, 8}));
        inventory.setSlot(2, {map, 1, 8});
        inventory.move(2, 0, content);
        const QcItemStack merged = inventory.slot(0);
        QCOMPARE(merged.count, 2);

        // Screens move the same way.
        QcSlotList screen{{{map, 1, 7}, {map, 1, 8}, {map, 2, 7}}};
        QVERIFY(screen.moveBetween(0, 1, 0, content));
        QCOMPARE(screen.stacks.at(0), (QcItemStack{map, 1, 8}));
        QCOMPARE(screen.stacks.at(1), (QcItemStack{map, 1, 7}));
        QCOMPARE(screen.moveInto(1, {0, 2}, content), 1);
        QCOMPARE(screen.stacks.at(2), (QcItemStack{map, 3, 7}));
        QCOMPARE(screen.stacks.at(0), (QcItemStack{map, 1, 8}));
    }

    void unknownBlocksOfAMapFileReadAsBlank()
    {
        // A map file naming a block no pack has any more (a removed mod) and a known one.
        QByteArray raw(qsizetype(QcMapData::pixelCount) * 2, '\0');
        qToLittleEndian(QcMapPixel::make(0, QcMapPixel::Shade::Normal), raw.data());
        qToLittleEndian(QcMapPixel::make(1, QcMapPixel::Shade::Bright), raw.data() + 2);
        QByteArray payload;
        QDataStream out(&payload, QIODevice::WriteOnly);
        out.setVersion(QcSaveFile::streamVersion);
        out << QcMapSaveCodec::overworld.toString() << quint8(0) << qint32(0) << qint32(0) << quint32(2);
        out << u"gone:ruby_block"_s << u"qtcraft:stone"_s << qCompress(raw);
        const QByteArray file = QcSaveFile::bytes(QcMapSaveCodec::magic, payload);

        const QcResult<QcMapData> map = QcMapSaveCodec::read(file, u"maps/1.qcmap"_s, *m_content);
        QVERIFY(map.has_value());
        QCOMPARE(map->at({0, 0}), QcMapPixel::blank);
        const std::optional<QcBlockId> stone = m_content->blockId(u"qtcraft:stone"_s);
        QVERIFY(stone);
        const quint16 known = map->at({1, 0});
        QCOMPARE(QcMapPixel::blockOf(known), std::optional<quint32>(m_content->state(*stone).type));
        QCOMPARE(QcMapPixel::shadeOf(known), QcMapPixel::Shade::Bright);
    }
};

QTEST_GUILESS_MAIN(TestMapStore)
#include "test_map_store.moc"
