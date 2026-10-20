#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcCreativeCatalog.h"
#include "QcTestUtil.h"

#include <QSet>
#include <QTest>

using namespace Qt::StringLiterals;

// The catalogue of the creative inventory (decision E-286): every item once, mods included, the
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

private slots:
    void everyItemIsListedOnce()
    {
        const QcCreativeCatalog catalog(*m_content, u"en"_s);
        QCOMPARE(catalog.entries().size(), m_content->itemCount());
        QSet<QcItemId> seen;
        for (const QcCreativeCatalog::Entry &entry : catalog.entries()) {
            QVERIFY(m_content->isValidItem(entry.item));
            seen.insert(entry.item);
        }
        QCOMPARE(seen.size(), m_content->itemCount());
        QCOMPARE(catalog.filter(QcCreativeCatalog::Category::All, {}).size(), m_content->itemCount());
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
            total += catalog.filter(C(tab), {}).size();
        }
        QCOMPARE(total, m_content->itemCount());
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
