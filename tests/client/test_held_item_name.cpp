#include "app/models/QcInventoryModel.h"
#include "common/content/registry/QcContentRegistry.h"

#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;

// When the HUD shows the name of the selected item over the hotbar (decision E-885): another slot
// selected, or another item (or custom name) in the selected slot; not when only the count changes
// nor for the first inventory of a world. The label follows the language.
class TestHeldItemName : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<QcContentRegistry> m_content = std::make_shared<QcContentRegistry>();
    QcItemId m_dirt = registerItem(u"t:dirt"_s);
    QcItemId m_stone = registerItem(u"t:stone"_s);

    QcItemId registerItem(const QString &name)
    {
        QcItemDefinition item;
        item.name = name;
        item.translationKey = name;
        return *m_content->registerItem(item);
    }

    // A hotbar with dirt in slot 0 and stone in slot 1, `selected` chosen.
    QcInventory hotbar(int selected, int dirtCount = 4) const
    {
        QcInventory inventory;
        inventory.setSlot(0, QcItemStack{m_dirt, dirtCount, 0});
        inventory.setSlot(1, QcItemStack{m_stone, 2, 0});
        inventory.setSelectedHotbarSlot(selected);
        return inventory;
    }

private slots:
    void anotherSlotOrItemIsAnnounced()
    {
        QcInventoryModel model;
        model.setContent(m_content, u"en"_s);
        QSignalSpy announced(&model, &QcInventoryModel::selectedItemAnnounced);

        // The first inventory of a world (joining) shows no name, but the label is there.
        QSignalSpy labelChanged(&model, &QcInventoryModel::selectedLabelChanged);
        model.setInventory(hotbar(0));
        QCOMPARE(announced.count(), 0);
        QCOMPARE(labelChanged.count(), 1);
        const QString dirt = model.selectedLabel();
        QVERIFY(!dirt.isEmpty());

        // More of the same: nothing to show.
        model.setInventory(hotbar(0, 9));
        QCOMPARE(announced.count(), 0);
        model.setInventory(hotbar(0, 9));
        QCOMPARE(announced.count(), 0);

        // Another slot (wheel, number key, pad).
        model.setInventory(hotbar(1));
        QCOMPARE(announced.count(), 1);
        QVERIFY(model.selectedLabel() != dirt);
        QVERIFY(!model.selectedLabel().isEmpty());

        // Another item arrives in the selected slot.
        QcInventory swapped = hotbar(1);
        swapped.setSlot(1, QcItemStack{m_dirt, 1, 0});
        model.setInventory(swapped);
        QCOMPARE(announced.count(), 2);
        QCOMPARE(model.selectedLabel(), dirt);

        // A custom name counts as another item; the label is the name.
        QcInventory named = swapped;
        named.setSlot(1, QcItemStack{m_dirt, 1, 0}.withName(u"Clod"_s));
        model.setInventory(named);
        QCOMPARE(announced.count(), 3);
        QCOMPARE(model.selectedLabel(), u"Clod"_s);

        // The slot emptied: announced with an empty label, so the HUD hides the name.
        QcInventory empty = named;
        empty.setSlot(1, QcItemStack{});
        model.setInventory(empty);
        QCOMPARE(announced.count(), 4);
        QVERIFY(model.selectedLabel().isEmpty());
    }

    void theLabelFollowsTheLanguageAndANewWorldIsQuiet()
    {
        QcInventoryModel model;
        model.setContent(m_content, u"en"_s);
        model.setInventory(hotbar(0));
        QSignalSpy announced(&model, &QcInventoryModel::selectedItemAnnounced);
        QSignalSpy labelChanged(&model, &QcInventoryModel::selectedLabelChanged);

        // Another language: the label is read again, nothing is shown.
        model.setContent(m_content, u"de"_s);
        QCOMPARE(labelChanged.count(), 1);
        model.setInventory(hotbar(1));
        QCOMPARE(announced.count(), 1);

        // The world left and joined again: its first inventory is quiet again.
        model.setContent(nullptr, u"en"_s);
        model.setContent(m_content, u"en"_s);
        model.setInventory(hotbar(0));
        QCOMPARE(announced.count(), 1);
        model.setInventory(hotbar(1));
        QCOMPARE(announced.count(), 2);
    }
};

QTEST_GUILESS_MAIN(TestHeldItemName)
#include "test_held_item_name.moc"
