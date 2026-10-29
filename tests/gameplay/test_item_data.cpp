// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/gameplay/QcInventory.h"
#include "common/gameplay/QcSlotList.h"
#include "common/protocol/QcProtocolCodec.h"
#include "QcTestUtil.h"
#include "server/items/QcItemEntityManager.h"
#include "server/storage/QcEntityCodec.h"
#include "server/storage/QcItemDataCodec.h"
#include "server/storage/QcLoadWarnings.h"
#include "server/storage/QcSaveFile.h"
#include "server/storage/QcStackReader.h"

#include <QDataStream>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;

// The optional data of item stacks (decision E-823): merge rules, the wire and the save format.
class TestItemData : public QObject
{
    Q_OBJECT

    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    QcItemId item(const char *name) const { return m_content->itemId(QString::fromLatin1(name)).value_or(qcNoItem); }

    QcItemStack named(const char *name, int count, const QString &text) const { return QcItemStack{item(name), count, 0}.withName(text); }

    // A backpack-like stack: one stick holding `contents`.
    QcItemStack holding(QList<QcItemStack> contents) const { return QcItemStack{item("qtcraft:stick"), 1, 0}.withContents(std::move(contents)); }

    // `depth` stacks inside each other.
    QcItemStack nested(int depth) const
    {
        QcItemStack inner{item("qtcraft:dirt"), 1, 0};
        for (int level = 0; level < depth; ++level) {
            inner = holding({inner});
        }
        return inner;
    }

    // A stack with `data` as another program might have written it: past withData, which refuses
    // data beyond the limits.
    static QcItemStack forged(const QcItemStack &plain, QcItemData data)
    {
        QcItemStack stack = plain;
        stack.data.reset(new QcSharedItemData(std::move(data)));
        return stack;
    }

    QcItemStack forgedHolding(QList<QcItemStack> contents) const
    {
        QcItemData data;
        data.contents = std::move(contents);
        return forged({item("qtcraft:stick"), 1, 0}, data);
    }

    // `depth` stacks inside each other, the outermost forged.
    QcItemStack forgedNested(int depth) const { return forgedHolding({nested(depth - 1)}); }

    // 40 eggs (maxStack 16) carrying a component of a mod, which may hold items.
    QcItemStack moddedEggs() const
    {
        QcItemData modded;
        modded.kept = {{0x8001, 1, false, "mod data"}};
        return QcItemStack{item("qtcraft:egg"), 40, 0}.withData(modded);
    }

    // Only the first part of a split stack keeps the unknown component; the others have no data.
    static void checkSplit(const QList<QcItemStack> &parts)
    {
        QCOMPARE(parts.size(), 3);
        QCOMPARE(parts.at(0).count, 16);
        QCOMPARE(parts.at(1).count, 16);
        QCOMPARE(parts.at(2).count, 8);
        QCOMPARE(parts.at(0).itemData().kept.size(), 1);
        QVERIFY(!parts.at(1).data);
        QVERIFY(!parts.at(2).data);
    }

    // The bytes of a stack on the wire.
    static QByteArray wire(const QcItemStack &stack)
    {
        QByteArray bytes;
        QDataStream out(&bytes, QIODevice::WriteOnly);
        QcProtocolCodec::writeStack(out, stack);
        return bytes;
    }

    // The stack read back from wire bytes; std::nullopt when the stream turned corrupt or bytes are left.
    static std::optional<QcItemStack> fromWire(const QByteArray &bytes)
    {
        QDataStream in(bytes);
        const QcItemStack stack = QcProtocolCodec::readStack(in);
        const bool clean = in.status() == QDataStream::Ok && in.atEnd();
        return clean ? std::optional(stack) : std::nullopt;
    }

    QByteArray saved(const QcItemStack &stack) const
    {
        QByteArray bytes;
        QDataStream out(&bytes, QIODevice::WriteOnly);
        out.setVersion(QcSaveFile::streamVersion);
        QcSaveFile::writeStack(out, stack, *m_content);
        return bytes;
    }

    // The stack read back from save bytes; std::nullopt for damaged data or bytes left.
    std::optional<QcItemStack> fromSave(const QByteArray &bytes, QcLoadWarnings &warnings) const
    {
        QDataStream in(bytes);
        in.setVersion(QcSaveFile::streamVersion);
        QcStackReader reader(*m_content, u"test"_s, warnings);
        const QcItemStack stack = reader.read(in);
        const bool clean = in.status() == QDataStream::Ok && in.atEnd();
        return clean ? std::optional(stack) : std::nullopt;
    }

    std::optional<QcItemStack> fromSave(const QByteArray &bytes) const
    {
        QcLoadWarnings warnings;
        return fromSave(bytes, warnings);
    }

    // A stack as a version before E-823 saved it: name, count, damage and nothing after.
    static QByteArray oldSave(const QString &name, qint32 count, qint32 damage)
    {
        QByteArray bytes;
        QDataStream out(&bytes, QIODevice::WriteOnly);
        out.setVersion(QcSaveFile::streamVersion);
        out << name << count << damage;
        return bytes;
    }

private slots:
    void plainStacksCarryNoData()
    {
        const QcItemStack plain{item("qtcraft:dirt"), 5, 0};
        QVERIFY(!plain.data);
        QVERIFY(!plain.withName(QString()).data);
        QVERIFY(!plain.withContents({{}, {}}).data);
        QVERIFY(!plain.withMapId(0).data);
        QVERIFY(plain.withName(u"x"_s).withName(QString()) == plain);
        // Trailing empty slots are left out, so equal contents compare equal.
        const QcItemStack dirt{item("qtcraft:dirt"), 1, 0};
        QCOMPARE(holding({dirt, {}, {}}).contents().size(), 1);
        QVERIFY(holding({dirt, {}}) == holding({dirt}));
        QCOMPARE(holding({{}, dirt}).contents().size(), 2);
        QCOMPARE(nested(2).itemData().depth(), 2);
    }

    void stacksWithDifferentDataNeverMerge()
    {
        const QcItemStack plain{item("qtcraft:dirt"), 1, 0};
        QVERIFY(plain.stacksWith(plain.withCount(5)));
        QVERIFY(!plain.stacksWith(plain.withName(u"Sand"_s)));
        QVERIFY(plain.withName(u"Sand"_s).stacksWith(plain.withName(u"Sand"_s)));
        QVERIFY(!plain.withName(u"Sand"_s).stacksWith(plain.withName(u"Soil"_s)));
        QVERIFY(!plain.withMapId(3).stacksWith(plain.withMapId(4)));

        // Stacks with contents never stack, not even with equal contents: each holds one item.
        const QcItemStack full = holding({plain});
        QVERIFY(!full.stacksWith(full));
        QCOMPARE(full.stackLimit(64), 1);
        QCOMPARE(plain.stackLimit(64), 64);
    }

    void inventoriesAndScreensKeepDataApart()
    {
        const QcContentRegistry &content = *m_content;
        QcInventory inventory;
        QCOMPARE(inventory.add(named("qtcraft:dirt", 3, u"Soil"_s), content), 0);
        QCOMPARE(inventory.add(item("qtcraft:dirt"), 2, content), 0);
        QCOMPARE(inventory.add(named("qtcraft:dirt", 4, u"Soil"_s), content), 0);
        const QcItemStack first = inventory.slot(0);
        const QcItemStack second = inventory.slot(1);
        QCOMPARE(first, named("qtcraft:dirt", 7, u"Soil"_s));
        QCOMPARE(second, (QcItemStack{item("qtcraft:dirt"), 2, 0}));

        // Two stacks with contents take two slots.
        const QcItemStack full = holding({{item("qtcraft:dirt"), 1, 0}});
        QCOMPARE(inventory.add(full.withCount(2), content), 0);
        QCOMPARE(inventory.slot(2), full);
        QCOMPARE(inventory.slot(3), full);
        inventory.move(2, 3, content);
        QCOMPARE(inventory.slot(2), full);

        QcSlotList screen{{named("qtcraft:dirt", 1, u"A"_s), named("qtcraft:dirt", 1, u"B"_s), full, {}}};
        QVERIFY(screen.moveBetween(0, 1, 0, content));
        QCOMPARE(screen.stacks.at(0), named("qtcraft:dirt", 1, u"B"_s));
        QCOMPARE(screen.stacks.at(1), named("qtcraft:dirt", 1, u"A"_s));
        QVERIFY(screen.moveBetween(2, 3, 0, content));
        QCOMPARE(screen.stacks.at(3), full);
    }

    void dataCrossesTheWire()
    {
        const QcItemStack map = QcItemStack{item("qtcraft:filled_map"), 2, 0}.withMapId(9);
        const QcItemStack full = holding({named("qtcraft:dirt", 3, u"Soil"_s), {}, map});
        for (const QcItemStack &stack : {map, full, named("qtcraft:stone", 1, u"Ä name"_s), nested(QcItemData::maxDepth)}) {
            const std::optional<QcItemStack> read = fromWire(wire(stack));
            QVERIFY(read);
            QCOMPARE(*read, stack);
        }

        // Components a save kept without knowing them stay off the wire.
        QcItemData kept = map.itemData();
        kept.kept = {{0x8001, 1, false, "mod"}};
        const std::optional<QcItemStack> read = fromWire(wire(map.withData(kept)));
        QVERIFY(read);
        QCOMPARE(*read, map);
    }

    void theWireRefusesWhatGoesBeyondItsLimits()
    {
        const QcItemStack dirt{item("qtcraft:dirt"), 1, 0};
        QcItemData longName;
        longName.name = QString(QcItemData::maxNameLength + 1, u'n');
        QVERIFY(fromWire(wire(dirt.withName(QString(QcItemData::maxNameLength, u'n')))));
        QVERIFY(!fromWire(wire(forged(dirt, longName))));
        QVERIFY(fromWire(wire(holding(QList<QcItemStack>(QcItemData::maxContents, dirt)))));
        QVERIFY(!fromWire(wire(forgedHolding(QList<QcItemStack>(QcItemData::maxContents + 1, dirt)))));
        QVERIFY(!fromWire(wire(forgedNested(QcItemData::maxDepth + 1))));
        QVERIFY(!fromWire(wire(forgedHolding(QList<QcItemStack>(QcItemData::maxContents, holding({dirt}))))));

        // A plain stack is as long as before item data; a component bit this version does not know.
        // Item id, count, damage; then the component bits of a stack with data.
        constexpr qsizetype bits = static_cast<qsizetype>(sizeof(QcItemId)) + 8;
        QCOMPARE(wire(dirt).size(), bits);
        QByteArray unknown = wire(dirt.withMapId(3));
        QCOMPARE(unknown.at(bits), char(1));
        unknown[bits] = static_cast<char>(8);
        QVERIFY(!fromWire(unknown));
    }

    void dataSurvivesASave()
    {
        const QcItemStack map = QcItemStack{item("qtcraft:filled_map"), 2, 0}.withMapId(9);
        const QcItemStack full = holding({named("qtcraft:dirt", 3, u"Soil"_s), {}, map});
        for (const QcItemStack &stack : {map, full, named("qtcraft:stone", 1, u"Stein"_s), QcItemStack{item("qtcraft:dirt"), 4, 0}}) {
            const std::optional<QcItemStack> read = fromSave(saved(stack));
            QVERIFY(read);
            QCOMPARE(*read, stack);
        }
        // A plain stack is saved as before E-823.
        QCOMPARE(saved(QcItemStack{item("qtcraft:dirt"), 4, 0}), oldSave(u"qtcraft:dirt"_s, 4, 0));
    }

    void unknownComponentsAreKept()
    {
        // The tail of a newer name component and a later version's component of an unknown id, in
        // the order they are written and read.
        QcItemData data = named("qtcraft:dirt", 1, u"Soil"_s).itemData();
        data.kept = {{static_cast<quint16>(QcItemData::Component::Name), 2, true, "italic"}, {0x8001, 3, false, "mod data"}};
        const QcItemStack stack = QcItemStack{item("qtcraft:dirt"), 1, 0}.withData(data);
        const QByteArray bytes = saved(stack);
        const std::optional<QcItemStack> read = fromSave(bytes);
        QVERIFY(read);
        QCOMPARE(read->name(), u"Soil"_s);
        QCOMPARE(read->itemData().kept, data.kept);
        QCOMPARE(saved(*read), bytes);
        // Kept components make the data differ: such a stack does not merge with a plain named one.
        QVERIFY(!read->stacksWith(named("qtcraft:dirt", 1, u"Soil"_s)));
    }

    void savedStacksAreFittedToTheirItem()
    {
        // Contents hold one item; names are cut to the longest name; unknown items inside are
        // dropped with a warning.
        const QcItemStack full = holding({{item("qtcraft:dirt"), 1, 0}});
        const std::optional<QcItemStack> one = fromSave(saved(full.withCount(5)));
        QVERIFY(one);
        QCOMPARE(one->count, 1);
        QcItemData longName;
        longName.name = QString(80, u'n');
        const std::optional<QcItemStack> cut = fromSave(saved(forged({item("qtcraft:dirt"), 1, 0}, longName)));
        QVERIFY(cut);
        QCOMPARE(cut->name().size(), QcItemData::maxNameLength);

        // The stone inside renamed to an item of the same name length no pack has.
        QByteArray bytes = saved(holding({{item("qtcraft:dirt"), 1, 0}, {item("qtcraft:stone"), 2, 0}}));
        const QByteArray stone = oldSave(u"qtcraft:stone"_s, 2, 0);
        QVERIFY(bytes.contains(stone));
        bytes.replace(stone, oldSave(u"qtcraft:nonex"_s, 2, 0));
        QcLoadWarnings warnings;
        const std::optional<QcItemStack> dropped = fromSave(bytes, warnings);
        QVERIFY(dropped);
        QCOMPARE(dropped->contents().size(), 1);
        QCOMPARE(warnings.texts().size(), 1);
        QVERIFY(warnings.texts().constFirst().contains(u"qtcraft:nonex"_s));
    }

    void damagedDataIsRefused()
    {
        const QcItemStack dirt{item("qtcraft:dirt"), 1, 0};
        QVERIFY(fromSave(saved(nested(QcItemData::maxDepth))));
        QVERIFY(!fromSave(saved(forgedNested(QcItemData::maxDepth + 1))));
        QVERIFY(!fromSave(saved(forgedHolding(QList<QcItemStack>(QcItemData::maxContents + 1, dirt)))));
        // 54 slots of one stack each are 108 stacks inside, beyond maxStacksInside.
        QVERIFY(!fromSave(saved(forgedHolding(QList<QcItemStack>(QcItemData::maxContents, holding({dirt}))))));
        QcItemData tooLong;
        tooLong.name = QString(QcItemDataCodec::maxSavedName + 1, u'n');
        QVERIFY(!fromSave(saved(forged(dirt, tooLong))));
        // Cut short inside the data.
        const QByteArray bytes = saved(named("qtcraft:dirt", 1, u"Soil"_s));
        QVERIFY(!fromSave(bytes.left(bytes.size() - 2)));
    }

    void droppedStacksKeepTheirDataOnceThroughASave()
    {
        // Items on the ground: named with a component of a mod, a map, a stack with contents (one
        // item: a forged count of 5 must not make five copies of its contents).
        QcItemData modded = named("qtcraft:dirt", 3, u"Soil"_s).itemData();
        modded.kept = {{0x8001, 1, false, "mod data"}};
        const QcItemStack soil = QcItemStack{item("qtcraft:dirt"), 3, 0}.withData(modded);
        const QcItemStack map = QcItemStack{item("qtcraft:filled_map"), 1, 0}.withMapId(9);
        const QcItemStack full = holding({named("qtcraft:dirt", 3, u"Soil"_s), {}, map});
        QcEntityRecord record;
        quint64 order = 1;
        for (const QcItemStack &stack : {soil, map, full, full.withCount(5)}) {
            QcItemEntityManager::Drop drop{stack, {1.5, 70.0, 1.5}, {}};
            record.items.append({drop, order++});
        }

        QcLoadWarnings warnings;
        const QByteArray bytes = QcEntityCodec::encode(record, *m_content);
        const QcResult<QcEntityRecord> read = QcEntityCodec::decode(bytes, {0, 0}, *m_content, warnings);
        QVERIFY(read);
        QCOMPARE(read->items.size(), 4);
        QCOMPARE(read->items.at(0).entry.stack, soil);
        QCOMPARE(read->items.at(0).entry.stack.itemData().kept.size(), 1);
        QCOMPARE(read->items.at(1).entry.stack, map);
        QCOMPARE(read->items.at(2).entry.stack, full);
        QCOMPARE(read->items.at(3).entry.stack, full);
        QCOMPARE(read->items.at(3).entry.stack.contents().size(), full.contents().size());

        // Saved again, the record is the same, byte for byte (the forged count is 1 from now on).
        record.items[3].entry.stack = full;
        QCOMPARE(QcEntityCodec::encode(*read, *m_content), QcEntityCodec::encode(record, *m_content));
        const QcResult<QcEntityRecord> again = QcEntityCodec::decode(QcEntityCodec::encode(*read, *m_content), {0, 0}, *m_content, warnings);
        QVERIFY(again);
        QCOMPARE(QcEntityCodec::encode(*again, *m_content), QcEntityCodec::encode(*read, *m_content));
        QVERIFY(warnings.texts().isEmpty());
    }

    void loadedDroppedStacksSplitWithTheirDataOnce()
    {
        QcEntityRecord record;
        record.items.append({QcItemEntityManager::Drop{moddedEggs(), {1.5, 70.0, 1.5}, {}}, 1});

        QcLoadWarnings warnings;
        const QcResult<QcEntityRecord> read = QcEntityCodec::decode(QcEntityCodec::encode(record, *m_content), {0, 0}, *m_content, warnings);
        QVERIFY(read);
        QList<QcItemStack> parts;
        for (const auto &entry : read->items) {
            parts.append(entry.entry.stack);
        }
        checkSplit(parts);
    }

    void spawnedStacksSplitWithTheirDataOnce()
    {
        QcItemEntityManager items(m_content, 1);
        QCOMPARE(items.spawn({moddedEggs(), {0.5, 70.0, 0.5}, {}}, 1), 1u);
        QCOMPARE(items.items().size(), 1);
        QCOMPARE(items.queued().size(), 2);
        checkSplit({items.items().at(0).stack, items.queued().at(0).stack, items.queued().at(1).stack});

        // A stack with contents is one item, whatever its count: no copies of what it holds.
        const QcItemStack full = holding({QcItemStack{item("qtcraft:dirt"), 3, 0}});
        QCOMPARE(items.spawn({full.withCount(5), {4.5, 70.0, 0.5}, {}}, 2), 2u);
        QCOMPARE(items.items().size(), 2);
        QCOMPARE(items.items().at(1).stack, full);
        QCOMPARE(items.queued().size(), 2);
    }

    void mapsSavedWithTheirIdInDamageGetItAsData()
    {
        const std::optional<QcItemStack> map = fromSave(oldSave(u"qtcraft:filled_map"_s, 3, 12));
        QVERIFY(map);
        QCOMPARE(map->mapId(), 12u);
        QCOMPARE(map->damage, 0);
        QCOMPARE(map->count, 3);
        // Other items keep their damage as wear.
        const std::optional<QcItemStack> tool = fromSave(oldSave(u"qtcraft:stone_pickaxe"_s, 1, 12));
        QVERIFY(tool);
        QCOMPARE(tool->damage, 12);
        QVERIFY(!tool->data);
    }
};

QTEST_GUILESS_MAIN(TestItemData)
#include "test_item_data.moc"
