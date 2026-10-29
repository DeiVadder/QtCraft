// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/registry/QcContentRegistry.h"
#include "common/content/registry/QcContentSerializer.h"
#include "common/gameplay/QcCreativeCatalog.h"
#include "QcTestUtil.h"

#include <QDataStream>
#include <QSet>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;

// The catalogue of the creative inventory (decision E-286): every item once (filled maps aside), mods included, the
// tabs, and the search by translated name, English name and id.
namespace {

QStringList namesOf(const QcContentRegistry &content, const QList<const QcCreativeCatalog::Entry *> &entries)
{
    QStringList names;
    for (const QcCreativeCatalog::Entry *entry : entries) {
        names.append(content.item(entry->item).name);
    }
    return names;
}

} // namespace

class TestCreativeCatalog : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    QcCreativeCatalog::Category categoryOf(const char *name) const
    {
        return QcCreativeCatalog::categoryOf(*m_content, m_content->item(*m_content->itemId(QString::fromLatin1(name))));
    }

    // Every item but the filled maps, which carry a map id (decision E-798).
    qsizetype listedCount() const
    {
        return std::ranges::count_if(m_content->items(), [](const QcItemDefinition &item) { return item.map != QcMapItem::Filled; });
    }

private slots:
    void everyItemIsListedOnce()
    {
        const QcCreativeCatalog catalog(*m_content, u"en"_s);
        QCOMPARE(catalog.entries().size(), listedCount());
        QVERIFY(listedCount() < m_content->itemCount());
        QSet<QcItemId> seen;
        for (const QcCreativeCatalog::Entry &entry : catalog.entries()) {
            QVERIFY(m_content->isValidItem(entry.item));
            seen.insert(entry.item);
        }
        QCOMPARE(seen.size(), listedCount());
        QCOMPARE(catalog.filter(QcCreativeCatalog::Category::All, {}).size(), listedCount());
    }

    void itemsGoToTheirTabs()
    {
        using C = QcCreativeCatalog::Category;
        QCOMPARE(categoryOf("qtcraft:stone"), C::Blocks);
        QCOMPARE(categoryOf("qtcraft:oak_planks"), C::Blocks);
        QCOMPARE(categoryOf("qtcraft:diamond_pickaxe"), C::Tools);
        QCOMPARE(categoryOf("qtcraft:diamond_helmet"), C::Tools);
        QCOMPARE(categoryOf("qtcraft:apple"), C::Food);
        QCOMPARE(categoryOf("qtcraft:water_bucket"), C::Items);
        QCOMPARE(categoryOf("qtcraft:diamond"), C::Items);
        // Circuit parts have their own tab (decision E-340); doors stay blocks.
        QCOMPARE(categoryOf("qtcraft:signal_dust"), C::Signal);
        QCOMPARE(categoryOf("qtcraft:sticky_signal_piston"), C::Signal);
        // The decoupled variants (decision E-870) are circuit parts too.
        for (const char *decoupled : {"qtcraft:decoupled_signal_piston", "qtcraft:decoupled_sticky_signal_piston", "qtcraft:decoupled_signal_dispenser",
                                      "qtcraft:decoupled_signal_dropper"}) {
            QCOMPARE(categoryOf(decoupled), C::Signal);
        }
        QCOMPARE(categoryOf("qtcraft:lever"), C::Signal);
        QCOMPARE(categoryOf("qtcraft:oak_door"), C::Blocks);

        // Building parts go to their tab; wooden buttons and plates made as parts are circuit parts.
        QCOMPARE(categoryOf("qtcraft:birch_button"), C::Signal);
        const auto part = std::ranges::find_if(m_content->blocks(), [](const QcBlockDefinition &block) {
            return !block.partOf.isEmpty() && !block.signal.isCircuitPart();
        });
        QVERIFY(part != m_content->blocks().end());
        QCOMPARE(QcCreativeCatalog::categoryOf(*m_content, m_content->item(*m_content->itemId(part->name))), C::Parts);

        // Tabs together hold every item exactly once; "all" lists them tab after tab.
        const QcCreativeCatalog catalog(*m_content, u"en"_s);
        qsizetype total = 0;
        for (int tab = 1; tab < QcCreativeCatalog::categoryCount; ++tab) {
            total += catalog.filter(static_cast<C>(tab), {}).size();
        }
        QCOMPARE(total, listedCount());
        QCOMPARE(catalog.entries().first().category, C::Blocks);
        QCOMPARE(catalog.entries().last().category, C::Items);
    }

    void searchFindsTranslatedNamesAndIds()
    {
        const QcCreativeCatalog german(*m_content, u"de"_s);
        using C = QcCreativeCatalog::Category;
        QVERIFY(namesOf(*m_content, german.filter(C::All, u"eichenbretter"_s)).contains(u"qtcraft:oak_planks"_s));
        // The English name and the id find it in German too.
        QVERIFY(namesOf(*m_content, german.filter(C::All, u"Oak Planks"_s)).contains(u"qtcraft:oak_planks"_s));
        QVERIFY(namesOf(*m_content, german.filter(C::All, u"qtcraft:oak_planks"_s)).contains(u"qtcraft:oak_planks"_s));
        // Every word must match; the tab narrows the search.
        QCOMPARE(namesOf(*m_content, german.filter(C::All, u"stein diamant zzz"_s)), QStringList());
        const QStringList pickaxes = namesOf(*m_content, german.filter(C::Tools, u"diamond pickaxe"_s));
        QCOMPARE(pickaxes, QStringList{u"qtcraft:diamond_pickaxe"_s});
        QVERIFY(german.filter(C::Food, u"pickaxe"_s).isEmpty());
    }

    // The client builds its catalogue from the content the server sent (decision E-874): the
    // building parts keep their material and family on the way, so their tab is not empty, and
    // every other tab has items too.
    void clientCatalogueHasEveryTab()
    {
        QByteArray bytes;
        {
            QDataStream out(&bytes, QIODevice::WriteOnly);
            QcContentSerializer::write(*m_content, out);
        }
        QDataStream in(bytes);
        const QcResult<QcContentRegistry> received = QcContentSerializer::read(in);
        QVERIFY2(received.has_value(), qcErrorText(received).constData());

        using C = QcCreativeCatalog::Category;
        const QcCreativeCatalog catalog(*received, u"en"_s);
        for (int tab = 0; tab < QcCreativeCatalog::categoryCount; ++tab) {
            QVERIFY2(!catalog.filter(static_cast<C>(tab), {}).isEmpty(), qPrintable(u"tab %1 is empty"_s.arg(tab)));
        }
        const QStringList parts = namesOf(*received, catalog.filter(C::Parts, {}));
        for (const QString &part : {u"qtcraft:oak_stairs"_s, u"qtcraft:oak_slab"_s, u"qtcraft:oak_vertical_slab"_s, u"qtcraft:oak_fence"_s,
                                    u"qtcraft:cobblestone_wall"_s, u"qtcraft:stone_stairs"_s}) {
            QVERIFY2(parts.contains(part), qPrintable(part));
        }
        // A part is only in its own tab.
        QVERIFY(!namesOf(*received, catalog.filter(C::Blocks, {})).contains(u"qtcraft:oak_stairs"_s));
        QCOMPARE(QcCreativeCatalog(*received, u"en"_s).filter(C::Parts, {}).size(), QcCreativeCatalog(*m_content, u"en"_s).filter(C::Parts, {}).size());
    }

    void modItemsAppear()
    {
        QcContentRegistry withMod = *m_content;
        QcItemDefinition wand;
        wand.name = u"testmod:wand"_s;
        wand.translationKey = u"item.testmod.wand"_s;
        QVERIFY(withMod.registerItem(wand));

        const QcCreativeCatalog catalog(withMod, u"en"_s);
        QCOMPARE(namesOf(withMod, catalog.filter(QcCreativeCatalog::Category::Items, u"wand"_s)), QStringList{u"testmod:wand"_s});
    }
};

QTEST_GUILESS_MAIN(TestCreativeCatalog)
#include "test_creative_catalog.moc"
