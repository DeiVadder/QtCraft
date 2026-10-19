#include "common/content/parser/QcContentParser.h"
#include "common/gameplay/QcCrafting.h"
#include "common/gameplay/QcInventory.h"
#include "QcTestUtil.h"

#include <QJsonDocument>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

QJsonObject json(const char *text)
{
    return QJsonDocument::fromJson(QByteArray(text)).object();
}

QcRecipeDefinition recipe(const char *text)
{
    const auto parsed = QcContentParser::parseRecipe(json(text), u"t"_s);
    if (!parsed) {
        QcTestUtil::failCase(QStringLiteral("recipe: %1").arg(parsed.error()));
        return {};
    }
    return *parsed;
}

} // namespace

class TestCrafting : public QObject
{
    Q_OBJECT

private:
    QcContentRegistry m_content;
    QcItemId m_plank = 0;
    QcItemId m_stick = 0;
    QcItemId m_stone = 0;
    QcItemId m_coal = 0;

    QList<QcItemStack> grid(int size, const QList<std::pair<int, QcItemId>> &cells) const
    {
        QList<QcItemStack> result(qsizetype(size) * size);
        for (const auto &[index, item] : cells) {
            result[index] = {item, 1, 0};
        }
        return result;
    }

    QString matched(const QList<QcItemStack> &stacks, int size) const
    {
        const std::optional<qsizetype> index = QcCrafting::match(m_content, stacks, size);
        return index ? m_content.recipes()[*index].name : QString();
    }

private slots:
    void initTestCase()
    {
        for (const char *name : {"t:plank", "t:stick", "t:stone", "t:coal", "t:pick", "t:torch", "t:ingot"}) {
            QcItemDefinition item;
            item.name = QString::fromLatin1(name);
            QVERIFY(m_content.registerItem(item).has_value());
        }

        m_plank = *m_content.itemId(u"t:plank"_s);
        m_stick = *m_content.itemId(u"t:stick"_s);
        m_stone = *m_content.itemId(u"t:stone"_s);
        m_coal = *m_content.itemId(u"t:coal"_s);

        m_content.registerRecipe(recipe(R"({"name":"pick","pattern":["SSS"," | "," | "],"key":{"S":"stone","|":"stick"},"result":{"item":"pick"}})"));
        m_content.registerRecipe(recipe(R"({"name":"stick","pattern":["P","P"],"key":{"P":"plank"},"result":{"item":"stick","count":4}})"));
        m_content.registerRecipe(recipe(R"({"name":"torch","pattern":["C ","| "],"key":{"C":"coal","|":"stick"},"result":{"item":"torch"}})"));
        m_content.registerRecipe(recipe(R"({"name":"ingot","type":"shapeless","ingredients":["stone","coal","coal"],"result":{"item":"ingot"}})"));
    }

    void parserTrimsPatterns()
    {
        const QcRecipeDefinition torch = m_content.recipes()[*m_content.recipeIndex(u"t:torch"_s)];
        // The blank right column is trimmed: the torch is a 1 x 2 shape.
        QCOMPARE(torch.width(), 1);
        QCOMPARE(torch.height(), 2);
        QCOMPARE(torch.itemAt(0, 0), u"t:coal"_s);
        QCOMPARE(QcCrafting::gridSizeNeeded(torch), 2);
        QCOMPARE(QcCrafting::gridSizeNeeded(m_content.recipes()[*m_content.recipeIndex(u"t:pick"_s)]), 3);
    }

    void parserRejectsBrokenRecipes_data()
    {
        QTest::addColumn<QByteArray>("document");

        QTest::newRow("unknown key") << QByteArray(R"({"name":"x","pattern":["AB"],"key":{"A":"stone"},"result":{"item":"x"}})");
        QTest::newRow("too wide") << QByteArray(R"({"name":"x","pattern":["AAAA"],"key":{"A":"stone"},"result":{"item":"x"}})");
        QTest::newRow("too tall") << QByteArray(R"({"name":"x","pattern":["A","A","A","A"],"key":{"A":"stone"},"result":{"item":"x"}})");
        QTest::newRow("too many keys") << QByteArray(R"({"name":"x","pattern":["A"],"key":{"A":"stone","B":"stone","C":"stone","D":"stone","E":"stone","F":"stone","G":"stone","H":"stone","I":"stone","J":"stone"},"result":{"item":"x"}})");
        QTest::newRow("long key") << QByteArray(R"({"name":"x","pattern":["A"],"key":{"AB":"stone"},"result":{"item":"x"}})");
        QTest::newRow("no result") << QByteArray(R"({"name":"x","pattern":["A"],"key":{"A":"stone"}})");
        QTest::newRow("empty shapeless") << QByteArray(R"({"name":"x","type":"shapeless","ingredients":[],"result":{"item":"x"}})");
        QTest::newRow("type") << QByteArray(R"({"name":"x","type":"smelting","result":{"item":"x"}})");
    }

    void parserRejectsBrokenRecipes()
    {
        QFETCH(QByteArray, document);
        QVERIFY(!QcContentParser::parseRecipe(QJsonDocument::fromJson(document).object(), u"t"_s).has_value());
    }

    void shapedRecipesMatchAnywhereInTheGrid()
    {
        // Stick: plank above plank, in any column and row of a 3 x 3 grid.
        QCOMPARE(matched(grid(3, {{0, m_plank}, {3, m_plank}}), 3), u"t:stick"_s);
        QCOMPARE(matched(grid(3, {{5, m_plank}, {8, m_plank}}), 3), u"t:stick"_s);
        QCOMPARE(matched(grid(2, {{1, m_plank}, {3, m_plank}}), 2), u"t:stick"_s);
        QCOMPARE(matched(grid(2, {{0, m_plank}, {1, m_plank}}), 2), QString());
        QCOMPARE(matched(grid(2, {{1, m_plank}, {3, m_plank}, {0, m_plank}}), 2), QString());

        // Pickaxe: three stone on top, sticks down the middle.
        QCOMPARE(matched(grid(3, {{0, m_stone}, {1, m_stone}, {2, m_stone}, {4, m_stick}, {7, m_stick}}), 3), u"t:pick"_s);

        // An extra item anywhere breaks the shape.
        QCOMPARE(matched(grid(3, {{0, m_stone}, {1, m_stone}, {2, m_stone}, {4, m_stick}, {7, m_stick}, {6, m_coal}}), 3),
                 QString());
        // A 3 x 3 shape never fits a 2 x 2 grid.
        QCOMPARE(matched(grid(2, {{0, m_stone}, {1, m_stone}}), 2), QString());
    }

    void shapedRecipesMayBeMirrored()
    {
        // Torch pattern "C" over "|" has no mirror difference; build an asymmetric one instead.
        QcContentRegistry content = m_content;
        content.registerRecipe(recipe(R"({"name":"hook","pattern":["S ","S|"],"key":{"S":"stone","|":"stick"},"result":{"item":"pick"}})"));
        const auto name = [&content](const QList<QcItemStack> &stacks) {
            const std::optional<qsizetype> index = QcCrafting::match(content, stacks, 2);
            return index ? content.recipes()[*index].name : QString();
        };
        QCOMPARE(name(grid(2, {{0, m_stone}, {2, m_stone}, {3, m_stick}})), u"t:hook"_s);
        QCOMPARE(name(grid(2, {{1, m_stone}, {3, m_stone}, {2, m_stick}})), u"t:hook"_s);
        QCOMPARE(name(grid(2, {{1, m_stone}, {3, m_stone}, {0, m_stick}})), QString());
    }

    void shapelessRecipesIgnoreOrderButNotCounts()
    {
        QCOMPARE(matched(grid(2, {{3, m_coal}, {0, m_stone}, {1, m_coal}}), 2), u"t:ingot"_s);
        QCOMPARE(matched(grid(3, {{8, m_coal}, {4, m_coal}, {2, m_stone}}), 3), u"t:ingot"_s);
        QCOMPARE(matched(grid(2, {{0, m_stone}, {1, m_coal}}), 2), QString());
        QCOMPARE(matched(grid(2, {{0, m_stone}, {1, m_coal}, {2, m_coal}, {3, m_coal}}), 2), QString());
    }

    void narrowTagsGoBeforeWideTags()
    {
        // "both" carries the tags t:narrow and t:wide, "wide" only t:wide.
        QcContentRegistry content;
        QcItemDefinition both;
        both.name = u"t:both"_s;
        both.tags = {u"t:narrow"_s, u"t:wide"_s};
        QcItemDefinition wide;
        wide.name = u"t:wide_only"_s;
        wide.tags = {u"t:wide"_s};
        QcItemDefinition result;
        result.name = u"t:result"_s;
        for (const QcItemDefinition &item : {both, wide, result}) {
            QVERIFY(content.registerItem(item).has_value());
        }

        // The wide tag is listed first; taken greedily it would grab "both" and leave nothing for
        // the narrow tag.
        QcRecipeDefinition shapeless;
        shapeless.name = u"t:mix"_s;
        shapeless.shaped = false;
        shapeless.ingredients = {u"#t:wide"_s, u"#t:narrow"_s};
        shapeless.resultItem = u"t:result"_s;
        content.registerRecipe(shapeless);
        const QcItemId bothId = *content.itemId(u"t:both"_s);
        const QcItemId wideId = *content.itemId(u"t:wide_only"_s);
        QCOMPARE(QcCrafting::requirements(content, shapeless),
                 (QList<std::pair<QString, int>>{{u"#t:narrow"_s, 1}, {u"#t:wide"_s, 1}}));

        QcInventory inventory;
        inventory.add(bothId, 1, content);
        inventory.add(wideId, 1, content);
        QCOMPARE(inventory.slot(0).item, bothId);
        QCOMPARE(QcCrafting::timesCraftable(content, inventory, shapeless, 64), 1);

        QcInventory attempt = inventory;
        QVERIFY(QcCrafting::consume(content, attempt, shapeless, 1));
        QCOMPARE(attempt.countOf(bothId) + attempt.countOf(wideId), 0);

        // The grid matches in the same order.
        QList<QcItemStack> cells(4);
        cells[0] = {bothId, 1, 0};
        cells[1] = {wideId, 1, 0};
        QCOMPARE(QcCrafting::match(content, cells, 2), std::optional<qsizetype>(0));

        // Two "wide" items do not make it: the narrow tag has nothing.
        cells[0] = {wideId, 1, 0};
        QVERIFY(!QcCrafting::match(content, cells, 2));
    }

    void requirementsCountEveryCell()
    {
        const QList<std::pair<QString, int>> needed =
            QcCrafting::requirements(m_content.recipes()[*m_content.recipeIndex(u"t:pick"_s)]);
        QCOMPARE(needed.size(), 2);
        for (const auto &[name, count] : needed) {
            QCOMPARE(count, name == u"t:stone"_s ? 3 : 2);
        }
    }

    void moveBetweenSplitsMergesAndSwaps()
    {
        QList<QcItemStack> stacks(4);
        stacks[0] = {m_plank, 10, 0};
        stacks[1] = {m_stone, 3, 0};
        QVERIFY(QcInventory::moveBetween(stacks, 0, 2, 1, m_content));
        QCOMPARE(stacks[0].count, 9);
        QCOMPARE(stacks[2], (QcItemStack{m_plank, 1, 0}));

        QVERIFY(QcInventory::moveBetween(stacks, 0, 2, 0, m_content));
        QCOMPARE(stacks[2].count, 10);
        QVERIFY(stacks[0].isEmpty());

        // Different items: a partial move does nothing, a whole-stack move swaps.
        QVERIFY(QcInventory::moveBetween(stacks, 1, 2, 1, m_content));
        QCOMPARE(stacks[1].count, 3);
        QVERIFY(QcInventory::moveBetween(stacks, 1, 2, 0, m_content));
        QCOMPARE(stacks[1].item, m_plank);
        QCOMPARE(stacks[2].item, m_stone);
        QVERIFY(!QcInventory::moveBetween(stacks, 0, 9, 0, m_content));
        QVERIFY(!QcInventory::moveBetween(stacks, -1, 0, 0, m_content));
    }
};

QTEST_GUILESS_MAIN(TestCrafting)
#include "test_crafting.moc"
