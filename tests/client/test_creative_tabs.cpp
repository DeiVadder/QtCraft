#include "app/models/QcCreativeItemModel.h"
#include "common/content/registry/QcContentRegistry.h"
#include "QcTestUtil.h"

#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;

// The tab names of the creative inventory (76.2-b): the handbook's category names where the
// handbook has the category, so tab and handbook chapter say the same; the UI's own names for the
// others and while there is no content.
class TestCreativeTabs : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    QString handbookName(const char *key, const QString &language) const
    {
        return m_content->translations().translate(u"handbook.category."_s + QString::fromLatin1(key), language);
    }

private slots:
    void handbookCategoriesTakeTheHandbookNames_data()
    {
        QTest::addColumn<QString>("language");
        QTest::newRow("de") << u"de"_s;
        QTest::newRow("en") << u"en"_s;
    }

    void handbookCategoriesTakeTheHandbookNames()
    {
        QFETCH(QString, language);
        QcCreativeItemModel model;
        QSignalSpy names(&model, &QcCreativeItemModel::categoryNamesChanged);
        model.setContent(m_content, language);

        QCOMPARE(names.count(), 1);
        const QStringList tabs = model.categoryNames();
        QCOMPARE(tabs.size(), QcCreativeCatalog::categoryCount);
        QCOMPARE(tabs.at(static_cast<int>(QcCreativeCatalog::Category::Blocks)), handbookName("blocks", language));
        QCOMPARE(tabs.at(static_cast<int>(QcCreativeCatalog::Category::Parts)), handbookName("parts", language));
        QCOMPARE(tabs.at(static_cast<int>(QcCreativeCatalog::Category::Tools)), handbookName("tools", language));
        QCOMPARE(tabs.at(static_cast<int>(QcCreativeCatalog::Category::Food)), handbookName("food", language));
        // The handbook has these texts in both languages, so no tab shows a key.
        for (const QString &tab : tabs) {
            QVERIFY2(!tab.startsWith(u"handbook."_s), qPrintable(tab));
        }
    }

    void germanToolsTabMatchesTheHandbook()
    {
        QcCreativeItemModel model;
        model.setContent(m_content, u"de"_s);

        QCOMPARE(model.categoryNames().at(static_cast<int>(QcCreativeCatalog::Category::Tools)), u"Werkzeuge und Rüstung"_s);
    }

    // No translator is installed here, so tr() gives the English source texts.
    void withoutContentTheUiNamesAreUsed()
    {
        const QcCreativeItemModel model;

        QCOMPARE(model.categoryNames(), (QStringList{u"All"_s, u"Blocks"_s, u"Building parts"_s, u"Signal Dust"_s,
                                                     u"Tools and armour"_s, u"Food"_s, u"Other items"_s}));
    }
};

QTEST_GUILESS_MAIN(TestCreativeTabs)
#include "test_creative_tabs.moc"
