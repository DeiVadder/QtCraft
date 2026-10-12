#include "QcBaseContent.h"
#include "QcContentPackLoader.h"
#include "QcContentParser.h"
#include "QcHandbookBuilder.h"
#include "QcHandbookJson.h"
#include "QcHandbookMarkdown.h"
#include "QcHandbookRules.h"
#include "QcKeyBindings.h"
#include "QcModLoader.h"
#include "QcTestUtil.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using Link = QcHandbookEntry::Link;

namespace {

std::shared_ptr<const QcContentRegistry> loadBase()
{
    QStringList warnings;
    QcResult<std::shared_ptr<QcContentRegistry>> loaded = QcBaseContent::load(warnings);
    return loaded ? std::shared_ptr<const QcContentRegistry>(*loaded) : nullptr;
}

std::shared_ptr<const QcContentRegistry> loadWithMods()
{
    QStringList warnings;
    QcResult<QcModLoader::Result> loaded = QcModLoader::load(QStringLiteral(QTCRAFT_MODS_SOURCE_DIR), warnings);
    return loaded ? std::shared_ptr<const QcContentRegistry>(loaded->content) : nullptr;
}

// Every link of an entry: facts, sections, recipe cells, results and machines.
QList<Link> linksOf(const QcHandbookEntry &entry)
{
    QList<Link> links;
    for (const QcHandbookEntry::Fact &fact : entry.facts) {
        links += fact.links;
    }

    for (const QcHandbookEntry::Section &section : entry.sections) {
        links += section.links;
        for (const QcHandbookEntry::Recipe &recipe : section.recipes) {
            links += recipe.cells;
            links.append(recipe.result);
            links += recipe.machines;
        }
    }
    return links;
}

const QcHandbookEntry::Fact *fact(const QcHandbookEntry &entry, const QString &label)
{
    const auto it = std::ranges::find(entry.facts, label, &QcHandbookEntry::Fact::label);
    return it == entry.facts.end() ? nullptr : &*it;
}

const QcHandbookEntry::Section *section(const QcHandbookEntry &entry, const QString &title)
{
    const auto it = std::ranges::find(entry.sections, title, &QcHandbookEntry::Section::title);
    return it == entry.sections.end() ? nullptr : &*it;
}

QStringList targets(const QList<Link> &links)
{
    QStringList result;
    for (const Link &link : links) {
        result.append(link.target);
    }
    return result;
}

// All recipes of an entry with their names.
QList<const QcHandbookEntry::Recipe *> recipesOf(const QcHandbookEntry &entry)
{
    QList<const QcHandbookEntry::Recipe *> recipes;
    for (const QcHandbookEntry::Section &section : entry.sections) {
        for (const QcHandbookEntry::Recipe &recipe : section.recipes) {
            recipes.append(&recipe);
        }
    }
    return recipes;
}

QString entryOfItem(const QcContentRegistry &content, const QString &item)
{
    if (const std::optional<QcBlockId> block = content.blockId(item); block && content.block(*block).renderLayer != QcRenderLayer::Invisible) {
        return u"block:"_s + item;
    }
    return u"item:"_s + item;
}

} // namespace

class TestHandbook : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_base;
    std::shared_ptr<const QcContentRegistry> m_modded;
    QcHandbook m_english;
    QcHandbook m_german;
    QcHandbook m_moddedEnglish;

private slots:
    void initTestCase()
    {
        m_base = loadBase();
        m_modded = loadWithMods();
        QVERIFY(m_base);
        QVERIFY(m_modded);

        QCOMPARE(m_modded->packs().size(), 2);

        QElapsedTimer timer;
        timer.start();
        m_english = QcHandbookBuilder::build(*m_base, u"en"_s, QcHandbookRules::values(), QcKeyBindings::defaults(QcKeyBindings::Platform::Other).placeholderKeys());
        qInfo("building the English handbook of the base game took %lld ms", timer.elapsed());
        m_german = QcHandbookBuilder::build(*m_base, u"de"_s, QcHandbookRules::values(), QcKeyBindings::defaults(QcKeyBindings::Platform::Other).placeholderKeys());
        m_moddedEnglish = QcHandbookBuilder::build(*m_modded, u"en"_s, QcHandbookRules::values(), QcKeyBindings::defaults(QcKeyBindings::Platform::Other).placeholderKeys());
    }

    void reportsNoProblems()
    {
        for (const QcHandbook *book : {&m_english, &m_german, &m_moddedEnglish}) {
            QVERIFY2(book->problems().isEmpty(), qPrintable(book->problems().join(u'\n')));
        }
    }

    void everyContentDefinitionHasAnEntry()
    {
        const QcContentRegistry &content = *m_modded;
        const QcHandbook &book = m_moddedEnglish;
        qsizetype visibleBlocks = 0;

        for (const QcBlockDefinition &block : content.blocks()) {
            if (block.renderLayer == QcRenderLayer::Invisible) {
                QVERIFY(!book.entry(u"block:"_s + block.name));
                continue;
            }
            ++visibleBlocks;
            const QcHandbookEntry *entry = book.entry(u"block:"_s + block.name);
            QVERIFY2(entry, qPrintable(block.name));
            const QString expected = block.liquid ? u"liquids"_s : (block.partOf.isEmpty() ? u"blocks"_s : u"parts"_s);
            QCOMPARE(entry->category, expected);
        }

        // Air is the only invisible block of the base game and the mod.
        QCOMPARE(visibleBlocks, content.blockCount() - 1);

        for (const QcItemDefinition &item : content.items()) {
            QVERIFY2(book.entry(entryOfItem(content, item.name)), qPrintable(item.name));
        }
        for (const QcMobDefinition &mob : content.mobs()) {
            const QcHandbookEntry *entry = book.entry(u"mob:"_s + mob.name);
            QVERIFY2(entry && entry->category == u"creatures"_s, qPrintable(mob.name));
        }
        for (const QcBiomeDefinition &biome : content.biomes()) {
            QVERIFY2(book.entry(u"biome:"_s + biome.name), qPrintable(biome.name));
        }
        for (const QcOreDefinition &ore : content.ores()) {
            QVERIFY2(book.entry(u"ore:"_s + ore.name), qPrintable(ore.name));
        }
        for (const QcFeatureDefinition &feature : content.features()) {
            QVERIFY2(book.entry(u"feature:"_s + feature.name), qPrintable(feature.name));
        }
        for (const QcHandbookChapter &chapter : content.handbookChapters()) {
            const QcHandbookEntry *entry = book.entry(u"chapter:"_s + chapter.name);
            QVERIFY2(entry && entry->sections.size() >= chapter.sections.size(), qPrintable(chapter.name));
        }

        QCOMPARE(content.handbookChapters().size(), 17);
        QVERIFY(book.entry(u"tag:qtcraft:planks"_s));
        QVERIFY(book.entry(u"world:terrain"_s));

        // Every entry sits in exactly one category.
        qsizetype listed = 0;
        for (const QcHandbook::Category &category : book.categories()) {
            listed += category.entries.size();
        }
        QCOMPARE(listed, book.entries().size());
    }

    void everyRecipeAppearsOnItsResultsPage()
    {
        const QcContentRegistry &content = *m_modded;
        const QcHandbook &book = m_moddedEnglish;
        QVERIFY(content.recipes().size() > 80);

        for (const QcRecipeDefinition &recipe : content.recipes()) {
            const QcHandbookEntry *entry = book.entry(entryOfItem(content, recipe.resultItem));
            QVERIFY2(entry, qPrintable(recipe.name));
            const QList<const QcHandbookEntry::Recipe *> recipes = recipesOf(*entry);
            const auto found = std::ranges::find_if(recipes, [&recipe](const QcHandbookEntry::Recipe *shown) {
                return shown->name == recipe.name && shown->kind == QcHandbookEntry::Recipe::Kind::Crafting;
            });
            QVERIFY2(found != recipes.end(), qPrintable(recipe.name));
            QCOMPARE((*found)->resultCount, recipe.resultCount);
            QCOMPARE((*found)->cells.size(), qsizetype((*found)->width) * (*found)->height);
        }

        const QcHandbookEntry *smeltingTable = book.entry(u"table:smelting"_s);
        QVERIFY(smeltingTable);
        const QList<const QcHandbookEntry::Recipe *> all = recipesOf(*smeltingTable);
        QCOMPARE(all.size(), content.smeltingRecipes().size());
        for (const QcSmeltingRecipe &recipe : content.smeltingRecipes()) {
            for (const QString &item : {recipe.resultItem, recipe.inputItem}) {
                const QcHandbookEntry *entry = book.entry(entryOfItem(content, item));
                QVERIFY2(entry, qPrintable(item));
                QVERIFY2(std::ranges::any_of(recipesOf(*entry), [&recipe](const QcHandbookEntry::Recipe *shown) {
                             return shown->name == recipe.name && shown->kind == QcHandbookEntry::Recipe::Kind::Machine;
                         }),
                         qPrintable(recipe.name));
            }
        }
    }

    void everyLinkResolves()
    {
        for (const QcHandbook *book : {&m_english, &m_german, &m_moddedEnglish}) {
            qsizetype links = 0;
            for (const QcHandbookEntry &entry : book->entries()) {
                const QList<Link> entryLinks = linksOf(entry);
                for (const Link &link : entryLinks) {
                    if (link.isEmpty()) {
                        continue;
                    }
                    ++links;
                    QVERIFY2(!link.target.isEmpty() && book->entry(link.target),
                             qPrintable(u"%1 -> '%2' (%3)"_s.arg(entry.id, link.target, link.text)));
                    QVERIFY2(!link.text.isEmpty(), qPrintable(entry.id + u" -> "_s + link.target));
                }
            }
            QVERIFY(links > 1000);
        }
    }

    void labelsAreTranslated()
    {
        // A label without translation shows its key ("handbook.fact.tool").
        for (const QcHandbook *book : {&m_english, &m_german, &m_moddedEnglish}) {
            for (const QcHandbook::Category &category : book->categories()) {
                QVERIFY2(!category.title.startsWith(u"handbook."_s), qPrintable(category.title));
            }
            for (const QcHandbookEntry &entry : book->entries()) {
                QVERIFY2(!entry.title.isEmpty() && !entry.title.contains(u"handbook."_s) && !entry.title.startsWith(u"chapter."_s),
                         qPrintable(entry.id));
                QStringList texts{entry.text};
                for (const QcHandbookEntry::Fact &fact : entry.facts) {
                    texts << fact.label << fact.value;
                    for (const Link &link : fact.links) {
                        texts << link.note;
                    }
                }
                for (const QcHandbookEntry::Section &section : entry.sections) {
                    texts << section.title << section.text;
                    for (const QcHandbookEntry::Recipe &recipe : section.recipes) {
                        texts << recipe.note;
                    }
                }
                for (const QString &text : std::as_const(texts)) {
                    QVERIFY2(!text.contains(u"handbook."_s) && !text.contains(u"%1"_s), qPrintable(entry.id + u": "_s + text));
                }
            }
        }

        for (const QString &key : {u"title"_s, u"intro"_s, u"contents"_s, u"index"_s, u"packs"_s, u"entries"_s}) {
            QVERIFY(m_german.label(key) != m_english.label(key) || key == u"title"_s);
        }
    }

    void basePagesHaveDescriptionsInBothLanguages()
    {
        // The base game describes every block, item, creature and biome in English and German.
        int described = 0;
        for (const QcHandbookEntry &entry : m_english.entries()) {
            if (!entry.id.startsWith(u"block:"_s) && !entry.id.startsWith(u"item:"_s) && !entry.id.startsWith(u"mob:"_s)
                && !entry.id.startsWith(u"biome:"_s)) {
                continue;
            }
            const QcHandbookEntry *german = m_german.entry(entry.id);
            QVERIFY(german);
            QVERIFY2(!entry.text.isEmpty(), qPrintable(entry.id));
            QVERIFY2(!german->text.isEmpty() && german->text != entry.text, qPrintable(entry.id));
            ++described;
        }
        QVERIFY(described > 140);
    }

    void missingDescriptionsLeaveTheFacts()
    {
        QcContentRegistry content = *m_base;
        QcBlockDefinition plain;
        plain.name = u"test:plain"_s;
        plain.translationKey = u"block.test.plain"_s;
        plain.descriptionKey = u"block.test.plain.description"_s;
        plain.hardness = 2.0;
        plain.drops = {{plain.name, 1, 1.0}};
        QVERIFY(content.registerBlock(plain).has_value());

        const QcHandbook bare = QcHandbookBuilder::build(content, u"en"_s, QcHandbookRules::values(), QcKeyBindings::defaults(QcKeyBindings::Platform::Other).placeholderKeys());
        QVERIFY(bare.entry(u"block:test:plain"_s)->text.isEmpty());

        // A mod's description may quote values; unknown names stay and are reported.
        content.addTranslations(u"en"_s, {{u"block.test.plain.description"_s, u"Hardness {test:plain.hardness}, {test:nothing.value}."_s}});
        const QcHandbook book = QcHandbookBuilder::build(content, u"en"_s, QcHandbookRules::values(), QcKeyBindings::defaults(QcKeyBindings::Platform::Other).placeholderKeys());
        const QcHandbookEntry *entry = book.entry(u"block:test:plain"_s);
        QVERIFY(entry);
        QCOMPARE(entry->text, u"Hardness 2, {test:nothing.value}."_s);
        QVERIFY(std::ranges::any_of(book.problems(), [](const QString &problem) { return problem.contains(u"{test:nothing.value}"_s); }));
        const QcHandbookEntry::Fact *hardness = fact(*entry, u"Hardness"_s);
        QVERIFY(hardness);
        QCOMPARE(hardness->value, u"2"_s);

        // 2 hardness points by hand at 1.5 s each.
        QCOMPARE(fact(*entry, u"Breaking by hand"_s)->value, u"3 s"_s);

        // No pack named "test" is loaded: its entries are grouped under the namespace after the packs.
        QCOMPARE(book.packs().constLast().id, u"test"_s);
    }

    void ironOreFactsMatchTheRules()
    {
        const QcHandbookEntry *ore = m_english.entry(u"block:qtcraft:iron_ore"_s);
        QVERIFY(ore);
        QCOMPARE(ore->title, u"Iron ore"_s);
        QCOMPARE(fact(*ore, u"ID"_s)->value, u"qtcraft:iron_ore"_s);
        QCOMPARE(fact(*ore, u"Hardness"_s)->value, u"3"_s);
        QCOMPARE(fact(*ore, u"Tool"_s)->value, u"Pickaxe, level 2 or better (needed for drops)"_s);

        // Without a stone pickaxe: 3 hardness × 5 s; with the diamond pickaxe (speed 8): 3 × 1.5 / 8.
        QCOMPARE(fact(*ore, u"Breaking by hand"_s)->value, u"15 s"_s);
        QCOMPARE(fact(*ore, u"Breaking with the best tool"_s)->value, u"0.56 s"_s);
        QCOMPARE(targets(fact(*ore, u"Breaking with the best tool"_s)->links), QStringList{u"item:qtcraft:diamond_pickaxe"_s});
        QCOMPARE(targets(fact(*ore, u"Drops"_s)->links), QStringList{u"block:qtcraft:iron_ore"_s});
        QCOMPARE(fact(*ore, u"Drops without the right tool"_s)->value, u"nothing"_s);

        const QcHandbookEntry::Section *minedWith = section(*ore, u"Mined with"_s);
        QVERIFY(minedWith);
        QCOMPARE(targets(minedWith->links), (QStringList{u"item:qtcraft:stone_pickaxe"_s, u"item:qtcraft:iron_pickaxe"_s,
                                                         u"item:qtcraft:diamond_pickaxe"_s}));

        const QcHandbookEntry::Section *smelts = section(*ore, u"Smelts into"_s);
        QVERIFY(smelts);
        QCOMPARE(smelts->recipes.size(), 1);
        const QcHandbookEntry::Recipe &recipe = smelts->recipes.first();
        QCOMPARE(recipe.result.target, u"item:qtcraft:iron_ingot"_s);
        QCOMPARE(recipe.seconds, 10.0);
        QCOMPARE(targets(recipe.machines), QStringList{u"block:qtcraft:furnace"_s});
        QCOMPARE(recipe.note, u"Furnace: 10 s per item, needs fuel"_s);

        QVERIFY(section(*ore, u"Found in"_s));
        QCOMPARE(targets(section(*ore, u"Found in"_s)->links), QStringList{u"ore:qtcraft:iron"_s});

        const QcHandbookEntry *german = m_german.entry(u"block:qtcraft:iron_ore"_s);
        QCOMPARE(german->title, u"Eisenerz"_s);
        QCOMPARE(fact(*german, u"Werkzeug"_s)->value, u"Spitzhacke, Stufe 2 oder besser (nötig für Beute)"_s);
        QCOMPARE(fact(*german, u"Abbauen mit dem besten Werkzeug"_s)->value, u"0,56 s"_s);

        // The ingot's page shows where it comes from and what it makes.
        const QcHandbookEntry *ingot = m_english.entry(u"item:qtcraft:iron_ingot"_s);
        QVERIFY(ingot);
        QCOMPARE(section(*ingot, u"Made in a furnace"_s)->recipes.first().cells.first().target, u"block:qtcraft:iron_ore"_s);
        const QStringList usedIn = targets(section(*ingot, u"Used in"_s)->links);
        for (const QString &result : {u"item:qtcraft:iron_pickaxe"_s, u"item:qtcraft:bucket"_s}) {
            QVERIFY2(usedIn.contains(result), qPrintable(result));
        }
    }

    void recipesAndFuelsMatchTheData()
    {
        // stick.json: "#" over "#" with "#planks" gives 4 sticks in the 2 x 2 grid.
        const QcHandbookEntry *stick = m_english.entry(u"item:qtcraft:stick"_s);
        const QcHandbookEntry::Recipe &recipe = section(*stick, u"How to make it"_s)->recipes.first();
        QCOMPARE(recipe.width, 1);
        QCOMPARE(recipe.height, 2);
        QCOMPARE(recipe.resultCount, 4);
        QCOMPARE(recipe.needsTable, false);
        QCOMPARE(targets(recipe.cells), (QStringList{u"tag:qtcraft:planks"_s, u"tag:qtcraft:planks"_s}));
        QCOMPARE(recipe.cells.first().text, u"Any planks"_s);

        const QcHandbookEntry *planks = m_english.entry(u"tag:qtcraft:planks"_s);
        QCOMPARE(targets(section(*planks, u"Items in this group"_s)->links),
                 (QStringList{u"block:qtcraft:birch_planks"_s, u"block:qtcraft:oak_planks"_s, u"block:qtcraft:spruce_planks"_s}));

        // The oak planks page lists the stick through the tag.
        const QcHandbookEntry::Section *usedIn = section(*m_english.entry(u"block:qtcraft:oak_planks"_s), u"Used in"_s);
        const auto stickLink = std::ranges::find(usedIn->links, u"item:qtcraft:stick"_s, &Link::target);
        QVERIFY(stickLink != usedIn->links.end());
        QCOMPARE(stickLink->note, u"Any planks"_s);

        // Fuels, longest first: lava bucket 1000 s, block of coal 720 s, then coal and charcoal 80 s.
        const QList<Link> &fuels = m_english.entry(u"table:fuels"_s)->sections.first().links;
        QCOMPARE(fuels.first().target, u"item:qtcraft:lava_bucket"_s);
        QCOMPARE(fuels.first().note, u"1000 s"_s);
        QCOMPARE(fuels[1].target, u"block:qtcraft:coal_block"_s);
        QCOMPARE(fuels[1].note, u"720 s"_s);
        QCOMPARE(fuels[2].target, u"item:qtcraft:charcoal"_s);
        QCOMPARE(fuels[3].target, u"item:qtcraft:coal"_s);
        QCOMPARE(fuels[3].note, u"80 s"_s);
        QCOMPARE(fact(*m_english.entry(u"block:qtcraft:oak_planks"_s), u"Burns as fuel"_s)->value, u"15 s"_s);

        // The furnace processes every smelting recipe of the base game.
        const QcHandbookEntry *furnace = m_english.entry(u"block:qtcraft:furnace"_s);
        QCOMPARE(section(*furnace, u"Processes"_s)->recipes.size(), m_base->smeltingRecipes().size());
    }

    void factsReadCorrectly()
    {
        const auto factIn = [](const QcHandbook &book, const QString &id, const QString &label) -> const QcHandbookEntry::Fact * {
            const QcHandbookEntry *entry = book.entry(id);
            return entry ? fact(*entry, label) : nullptr;
        };

        // Glass lets all light through: nothing is blocked.
        const QcHandbookEntry::Fact *glass = factIn(m_english, u"block:qtcraft:glass"_s, u"Light blocked"_s);
        QVERIFY(glass);
        QCOMPARE(glass->value, u"0 of 15"_s);
        const QcHandbookEntry::Fact *glassGerman = factIn(m_german, u"block:qtcraft:glass"_s, u"Lichtdämpfung"_s);
        QVERIFY(glassGerman);
        QCOMPARE(glassGerman->value, u"0 von 15"_s);

        const QcHandbookEntry::Fact *furnace = factIn(m_english, u"block:qtcraft:furnace"_s, u"Machine"_s);
        QVERIFY(furnace);
        QCOMPARE(furnace->value, u"smelting, speed × 1"_s);
        const QcHandbookEntry::Fact *furnaceGerman = factIn(m_german, u"block:qtcraft:furnace"_s, u"Maschine"_s);
        QVERIFY(furnaceGerman);
        QCOMPARE(furnaceGerman->value, u"Schmelzen, Tempo × 1"_s);

        const QcHandbookEntry *ironOre = m_german.entry(u"block:qtcraft:iron_ore"_s);
        QVERIFY(ironOre && !ironOre->sections.isEmpty() && !ironOre->sections.first().recipes.isEmpty());
        QVERIFY(ironOre->sections.first().recipes.first().note.startsWith(u"Ofen: 10 s"_s));

        const QcHandbookEntry::Fact *lavaWater = factIn(m_english, u"block:qtcraft:lava"_s, u"Water nearby"_s);
        QVERIFY(lavaWater);
        QCOMPARE(targets(lavaWater->links), (QStringList{u"block:qtcraft:obsidian"_s, u"block:qtcraft:cobblestone"_s}));

        const QcHandbookEntry::Fact *fireflyHealth = factIn(m_english, u"mob:qtcraft:firefly"_s, u"Health"_s);
        QVERIFY(fireflyHealth);
        QCOMPARE(fireflyHealth->value, u"1 health point"_s);
        const QcHandbookEntry::Fact *fireflyHealthGerman = factIn(m_german, u"mob:qtcraft:firefly"_s, u"Gesundheit"_s);
        QVERIFY(fireflyHealthGerman);
        QCOMPARE(fireflyHealthGerman->value, u"1 Lebenspunkt"_s);
        const QcHandbookEntry::Fact *fireflySpeed = factIn(m_english, u"mob:qtcraft:firefly"_s, u"Speed"_s);
        QVERIFY(fireflySpeed);
        QCOMPARE(fireflySpeed->value, u"0.8 m/s"_s);

        const QcHandbookEntry *firefly = m_english.entry(u"mob:qtcraft:firefly"_s);
        QVERIFY(firefly);
        QVERIFY(!fact(*firefly, u"Spawn light"_s));
        const QcHandbookEntry::Section *fireflyGround = section(*firefly, u"Appears on"_s);
        QVERIFY(fireflyGround);
        QCOMPARE(fireflyGround->text, u"Any solid block or the surface of a liquid."_s);

        const QcHandbookEntry::Fact *pigSpeed = factIn(m_english, u"mob:qtcraft:pig"_s, u"Speed"_s);
        QVERIFY(pigSpeed);
        QVERIFY(pigSpeed->value.contains(u"fleeing"_s));
        const QcHandbookEntry *pig = m_english.entry(u"mob:qtcraft:pig"_s);
        QVERIFY(pig);
        const QcHandbookEntry::Section *pigGround = section(*pig, u"Appears on"_s);
        QVERIFY(pigGround);
        QCOMPARE(pigGround->links.isEmpty(), false);

        const QcHandbookEntry *zombie = m_english.entry(u"mob:qtcraft:zombie"_s);
        QVERIFY(zombie);
        const QcHandbookEntry::Section *biomes = section(*zombie, u"Appears in biomes"_s);
        QVERIFY(biomes);
        QCOMPARE(biomes->text, u"All biomes."_s);
        QVERIFY(biomes->links.isEmpty());
    }

    void lightBlockingShowsTheEffectiveValue()
    {
        // Partial shapes block light only at the sides they cover (E-104): a cobweb lets all light
        // through, slabs and stairs only hold it at their covered sides, a full block keeps its value.
        const auto lightFact = [](const QcHandbook &book, const QString &id) -> QString {
            const QcHandbookEntry *entry = book.entry(id);
            const QcHandbookEntry::Fact *blocked = nullptr;
            for (const QString &label : {u"Light blocked"_s, u"Lichtdämpfung"_s}) {
                blocked = blocked ? blocked : (entry ? fact(*entry, label) : nullptr);
            }
            return blocked ? blocked->value : u"-"_s;
        };
        QCOMPARE(lightFact(m_english, u"block:qtcraft:cobweb"_s), u"0 of 15"_s);
        QCOMPARE(lightFact(m_english, u"block:qtcraft:oak_fence"_s), u"0 of 15"_s);
        QCOMPARE(lightFact(m_english, u"block:qtcraft:stone_slab"_s), u"only at the sides its shape covers fully"_s);
        QCOMPARE(lightFact(m_german, u"block:qtcraft:stone_stairs"_s), u"nur an den Seiten, die seine Form ganz bedeckt"_s);
        QCOMPARE(lightFact(m_english, u"block:qtcraft:ice"_s), u"2 of 15"_s);
        QCOMPARE(lightFact(m_english, u"block:qtcraft:stone"_s), u"-"_s);

        // No block's data claims a dimming that its shape cancels: a light-dimming block whose
        // states all cover no side fully would have no effect.
        for (const QcContentRegistry *content : {m_base.get(), m_modded.get()}) {
            const QcBlockStateTable &states = content->stateTable();
            for (qsizetype typeIndex = 0; typeIndex < states.typeCount(); ++typeIndex) {
                const quint32 type = quint32(typeIndex);
                bool partial = false;
                bool coversSides = false;
                for (int index = 0; index < states.stateCount(type); ++index) {
                    const QcBlockState &state = states.state(states.firstState(type) + quint32(index));
                    partial = partial || !state.fullCube;
                    coversSides = coversSides || state.lightBlockedFaces != 0;
                }
                const QcBlockDefinition &block = content->block(states.firstState(type));
                QVERIFY2(!partial || coversSides || block.lightOpacity == 0, qPrintable(block.name));
            }
        }
    }

    void machineCategoryWithoutTranslationShowsItsId()
    {
        QcContentRegistry content = *m_base;
        QcBlockDefinition grinder;
        grinder.name = u"test:grinder"_s;
        grinder.translationKey = u"block.test.grinder"_s;
        grinder.hardness = 2.0;
        grinder.machine.category = u"test_grinding"_s;
        grinder.machine.speed = 2.0;
        grinder.drops = {{grinder.name, 1, 1.0}};
        QVERIFY(content.registerBlock(grinder).has_value());

        for (const QString &language : {u"en"_s, u"de"_s}) {
            const QcHandbook book = QcHandbookBuilder::build(content, language, QcHandbookRules::values(), QcKeyBindings::defaults(QcKeyBindings::Platform::Other).placeholderKeys());
            const QcHandbookEntry *entry = book.entry(u"block:test:grinder"_s);
            QVERIFY(entry);
            const QcHandbookEntry::Fact *machine = fact(*entry, language == u"en"_s ? u"Machine"_s : u"Maschine"_s);
            QVERIFY2(machine, qPrintable(language));
            QCOMPARE(machine->value, language == u"en"_s ? u"test_grinding, speed × 2"_s : u"test_grinding, Tempo × 2"_s);
        }
    }

    void unknownPlaceholdersAreReportedOncePerText()
    {
        QcContentRegistry content = *m_base;
        QcBlockDefinition noisy;
        noisy.name = u"test:noisy"_s;
        noisy.translationKey = u"block.test.noisy"_s;
        noisy.descriptionKey = u"block.test.noisy.description"_s;
        noisy.hardness = 1.0;
        QVERIFY(content.registerBlock(noisy).has_value());

        // The same unknown name 200 times and 40 different ones in a single text.
        QString text = u"Hardness {test:noisy.hardness}."_s;
        for (int i = 0; i < 200; ++i) {
            text += u" {test:missing}"_s;
        }
        for (int i = 0; i < 40; ++i) {
            text += u" {test:other%1}"_s.arg(i);
        }

        content.addTranslations(u"en"_s, {{u"block.test.noisy.description"_s, text}});
        const QcHandbook book = QcHandbookBuilder::build(content, u"en"_s, QcHandbookRules::values(), QcKeyBindings::defaults(QcKeyBindings::Platform::Other).placeholderKeys());
        const QcHandbookEntry *entry = book.entry(u"block:test:noisy"_s);
        QVERIFY(entry);
        QVERIFY(entry->text.startsWith(u"Hardness 1. {test:missing} {test:missing}"_s));

        const QStringList reports = book.problems().filter(u"test:noisy: "_s);
        QCOMPARE(reports.size(), 1);
        const QString &report = reports.first();
        QCOMPARE(report.count(u"{test:missing}"_s), 1);
        QVERIFY(report.contains(u"{test:other0}"_s));

        // 41 distinct names: a few are named, the rest counted.
        QVERIFY2(!report.contains(u"{test:other39}"_s), qPrintable(report));
        QVERIFY2(report.contains(u"and 33 more"_s), qPrintable(report));
    }

    void languageFilesKeepDotsOtherThanTheHandbookSuffix()
    {
        QTemporaryDir dir;
        const auto write = [&dir](const QString &name, const QByteArray &content) {
            QDir().mkpath(QFileInfo(dir.filePath(name)).path());
            QFile file(dir.filePath(name));
            return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
        };

        QVERIFY(write(u"pack.json"_s, R"({"id":"langs","version":"1"})"));
        QVERIFY(write(u"lang/en.json"_s, R"({"a":"A"})"));
        QVERIFY(write(u"lang/de.handbook.json"_s, R"({"b":"B"})"));
        QVERIFY(write(u"lang/pt.BR.json"_s, R"({"c":"C"})"));

        QcContentRegistry content;
        QStringList warnings;
        QVERIFY(QcContentPackLoader::load(dir.path(), content, warnings).has_value());

        QStringList languages = content.languages();
        languages.sort();
        QCOMPARE(languages, (QStringList{u"de"_s, u"en"_s, u"pt.BR"_s}));
        QCOMPARE(content.translate(u"b"_s, u"de"_s), u"B"_s);
    }

    void creaturePagesShowSpawnRules()
    {
        const QcHandbookEntry *zombie = m_english.entry(u"mob:qtcraft:zombie"_s);
        QVERIFY(zombie);
        QCOMPARE(fact(*zombie, u"Appears"_s)->value, u"at night"_s);
        QCOMPARE(fact(*zombie, u"Spawn light"_s)->value, u"block light up to 7"_s);
        QCOMPARE(fact(*zombie, u"Limit"_s)->value, u"all hostile creatures together: 8 per player"_s);
        QCOMPARE(fact(*zombie, u"Attack"_s)->value, u"3 damage within 1.6 m, every 1 s"_s);
        // Rarely a carrot or a potato as well (decision E-294).
        QCOMPARE(targets(fact(*zombie, u"Drops"_s)->links),
                 (QStringList{u"item:qtcraft:rotten_flesh"_s, u"item:qtcraft:carrot"_s, u"item:qtcraft:potato"_s}));
        QVERIFY(targets(section(*zombie, u"Appears on"_s)->links).contains(u"block:qtcraft:stone"_s));

        const QcHandbookEntry *firefly = m_english.entry(u"mob:qtcraft:firefly"_s);
        QCOMPARE(fact(*firefly, u"Limit"_s)->value, u"8 near each player; all ambient creatures together: 10 per player"_s);
        QCOMPARE(targets(section(*firefly, u"Appears in biomes"_s)->links), QStringList{u"biome:qtcraft:swamp"_s});
        QCOMPARE(targets(fact(*firefly, u"Can be caught"_s)->links),
                 (QStringList{u"item:qtcraft:glass_bottle"_s, u"item:qtcraft:firefly_bottle"_s}));
        const QcHandbookEntry *bottle = m_english.entry(u"item:qtcraft:firefly_bottle"_s);
        QCOMPARE(targets(section(*bottle, u"Caught from"_s)->links), QStringList{u"mob:qtcraft:firefly"_s});

        const QcHandbookEntry *swamp = m_english.entry(u"biome:qtcraft:swamp"_s);
        QVERIFY(targets(section(*swamp, u"Creatures"_s)->links).contains(u"mob:qtcraft:firefly"_s));
        QVERIFY(targets(section(*swamp, u"Plants and trees"_s)->links).contains(u"feature:qtcraft:sugar_cane"_s));
        QCOMPARE(targets(fact(*swamp, u"Under water"_s)->links), QStringList{u"block:qtcraft:clay"_s});
    }

    void chaptersQuoteTheRuleValues()
    {
        const QcHandbookEntry *hunger = m_english.entry(u"chapter:qtcraft:hunger"_s);
        QVERIFY(hunger);
        QVERIFY(hunger->sections.first().text.contains(u"holds 20 points"_s));
        QVERIFY(hunger->sections[1].text.contains(u"With at least 18 food points you heal one health point every 4 s"_s));

        for (const QcHandbook *book : {&m_english, &m_german, &m_moddedEnglish}) {
            for (const QcHandbookEntry &entry : book->entries()) {
                QVERIFY2(!entry.text.contains(u'{'), qPrintable(entry.id));
                for (const QcHandbookEntry::Section &section : entry.sections) {
                    QVERIFY2(!section.text.contains(u'{'), qPrintable(entry.id));
                }
            }
        }

        // Descriptions quote content values: the chest's slot count and the ore's tool level.
        const std::optional<QcBlockId> chest = m_base->blockId(u"qtcraft:chest"_s);
        QVERIFY(chest);
        QVERIFY(m_english.entry(u"block:qtcraft:chest"_s)->text.startsWith(
            u"Stores %1 stacks."_s.arg(m_base->block(*chest).containerSlots)));
        QVERIFY(m_german.entry(u"block:qtcraft:iron_ore"_s)->text.startsWith(u"Braucht eine Spitzhacke der Stufe 2 oder besser."_s));
        QVERIFY(m_moddedEnglish.entry(u"block:rubies:ruby_ore"_s)->text.contains(u"level 3."_s));
        QVERIFY(m_english.entry(u"chapter:qtcraft:mining"_s)->sections[1].text.contains(u"wood is level 1, stone 2, iron 3, diamond 4."_s));

        // Without the server's values the placeholders stay and are reported.
        const QcHandbook bare = QcHandbookBuilder::build(*m_base, u"en"_s);
        QVERIFY(std::ranges::any_of(bare.problems(), [](const QString &problem) { return problem.contains(u"{hunger.maxFood}"_s); }));
    }

    void controlsShowTheDefaultKeys()
    {
        const QcHandbookEntry *controls = m_english.entry(u"chapter:qtcraft:controls"_s);
        QVERIFY(controls);
        const QString keys = controls->sections.first().text;
        QVERIFY2(keys.contains(u"**W A S D**: walk"_s), qPrintable(keys));
        QVERIFY2(keys.contains(u"**V / F5**: camera view, **H / F1**: handbook, **I / F3**: debug display (off, compact, full), **O / F4**: debug options, **Esc**: menu"_s),
                 qPrintable(keys));
        QVERIFY2(keys.contains(u"**T / Enter**: chat"_s), qPrintable(keys));
        QVERIFY2(keys.contains(u"**1–9**"_s), qPrintable(keys));

        const QString german = m_german.entry(u"chapter:qtcraft:controls"_s)->sections.first().text;
        QVERIFY2(german.contains(u"**Leertaste**: springen"_s), qPrintable(german));
        QVERIFY(m_english.entry(u"chapter:qtcraft:controls"_s)->sections[1].text.contains(u"hold fn"_s));
        QCOMPARE(m_english.label(u"intro"_s).contains(u"(H / F1)"_s), true);
        QVERIFY2(keys.contains(u"**Ctrl**: sprint"_s) && keys.contains(u"**Ctrl+Q**: the whole stack"_s), qPrintable(keys));

        // In the game on a Mac the keys are named like the Mac keyboard and the controls page.
        const QcHandbook mac = QcHandbookBuilder::build(*m_base, u"en"_s, QcHandbookRules::values(),
                                                        QcKeyBindings::defaults(QcKeyBindings::Platform::Mac).placeholderKeys());
        const QString macKeys = mac.entry(u"chapter:qtcraft:controls"_s)->sections.first().text;
        QVERIFY2(macKeys.contains(u"**Option**: sprint"_s) && macKeys.contains(u"**Control+Q**: the whole stack"_s), qPrintable(macKeys));

        // The player's keys replace the defaults.
        QcKeyBindings rebound = QcKeyBindings::defaults(QcKeyBindings::Platform::Other);
        rebound.bind(QcKeyBindings::Action::CameraView, 0, Qt::Key_C);
        const QcHandbook book = QcHandbookBuilder::build(*m_base, u"en"_s, QcHandbookRules::values(), rebound.placeholderKeys());
        QVERIFY(book.entry(u"chapter:qtcraft:controls"_s)->sections.first().text.contains(u"**C / F5**: camera view"_s));
    }

    void modContentHasItsOwnSection()
    {
        const QcHandbook &book = m_moddedEnglish;
        QCOMPARE(book.packs().size(), 2);
        QCOMPARE(book.packs()[1].name, u"Rubies"_s);

        for (const QString &id : {u"block:rubies:ruby_ore"_s, u"item:rubies:ruby"_s, u"mob:rubies:crystal_beetle"_s,
                                  u"block:rubies:molten_ruby"_s, u"feature:rubies:ruby_rose"_s, u"ore:rubies:ruby"_s,
                                  u"block:rubies:ruby_pillar"_s}) {
            const QcHandbookEntry *entry = book.entry(id);
            QVERIFY2(entry && entry->pack == u"rubies"_s, qPrintable(id));
            QVERIFY2(!entry->text.isEmpty() || id.startsWith(u"feature:"_s) || id.startsWith(u"ore:"_s), qPrintable(id));

            // Within its category the mod's entries follow every entry of the base game.
            const QList<qsizetype> &entries = book.category(entry->category)->entries;
            const qsizetype position = entries.indexOf(qsizetype(entry - book.entries().data()));
            QVERIFY(position >= 0);
            for (qsizetype i = position; i < entries.size(); ++i) {
                QCOMPARE(book.entries()[entries[i]].pack, u"rubies"_s);
            }
        }

        // The mod's ore shows up among the base game's pickaxes and its pickaxe on the base game's ore.
        QVERIFY(targets(section(*book.entry(u"block:qtcraft:diamond_ore"_s), u"Mined with"_s)->links).contains(u"item:rubies:ruby_pickaxe"_s));

        const QList<QcHandbookMarkdown::File> files = QcHandbookMarkdown::files(book);
        const auto blocks = std::ranges::find(files, u"blocks.md"_s, &QcHandbookMarkdown::File::name);
        QVERIFY(blocks != files.end());
        const QString text = QString::fromUtf8(blocks->content);
        const qsizetype rubiesHeading = text.indexOf(u"\n## Rubies\n"_s);
        QVERIFY(rubiesHeading > text.indexOf(u"\n## QtCraft\n"_s));
        QVERIFY(text.indexOf(u"<a id=\"block-rubies-ruby_ore\"></a>"_s) > rubiesHeading);
        QVERIFY(text.indexOf(u"<a id=\"block-qtcraft-stone\"></a>"_s) < rubiesHeading);
    }

    void searchFindsEntriesInBothLanguages()
    {
        const auto first = [](const QcHandbook &book, const QString &query) {
            const QList<qsizetype> found = book.search(query);
            return found.isEmpty() ? QString() : book.entries()[found.first()].id;
        };

        QCOMPARE(first(m_german, u"Eisenerz"_s), u"block:qtcraft:iron_ore"_s);
        QCOMPARE(first(m_german, u"iron ore"_s), u"block:qtcraft:iron_ore"_s);
        QCOMPARE(first(m_english, u"iron ore"_s), u"block:qtcraft:iron_ore"_s);
        QCOMPARE(first(m_english, u"IRON OR"_s), u"block:qtcraft:iron_ore"_s);

        // Case and accents do not matter.
        QCOMPARE(first(m_german, u"glühwürmchen"_s), u"mob:qtcraft:firefly"_s);
        QCOMPARE(first(m_german, u"GLUHWURMCHEN"_s), u"mob:qtcraft:firefly"_s);
        QCOMPARE(first(m_german, u"Hunger"_s), u"chapter:qtcraft:hunger"_s);

        // Words may come from the text: "keepInventory" is explained in a chapter.
        QVERIFY(m_english.search(u"keepinventory"_s).contains(qsizetype(m_english.entry(u"chapter:qtcraft:rules"_s) - m_english.entries().data())));
        const QList<qsizetype> pickaxes = m_english.search(u"pickaxe"_s);
        QVERIFY(pickaxes.size() >= 4);
        const QList<qsizetype> firstFour = pickaxes.first(4);
        for (const qsizetype index : firstFour) {
            QVERIFY(m_english.entries()[index].title.endsWith(u"pickaxe"_s));
        }

        QVERIFY(m_english.search(u"no such thing"_s).isEmpty());
        QVERIFY(m_english.search(u"   "_s).isEmpty());
        QCOMPARE(m_english.search(u"a"_s, 5).size(), 5);
    }

    void exportIsDeterministicAndLinked()
    {
        // Two independent loads and builds give the same bytes.
        const std::shared_ptr<const QcContentRegistry> again = loadWithMods();
        const QList<QcHandbookMarkdown::File> first = QcHandbookMarkdown::files(m_moddedEnglish);
        const QList<QcHandbookMarkdown::File> second =
            QcHandbookMarkdown::files(QcHandbookBuilder::build(*again, u"en"_s, QcHandbookRules::values(), QcKeyBindings::defaults(QcKeyBindings::Platform::Other).placeholderKeys()));
        QCOMPARE(first.size(), second.size());
        for (qsizetype i = 0; i < first.size(); ++i) {
            QCOMPARE(first[i].name, second[i].name);
            QVERIFY2(first[i].content == second[i].content, qPrintable(first[i].name));
        }

        // Every relative link names an existing file and an anchor defined in it.
        QHash<QString, QSet<QString>> anchors;
        static const QRegularExpression anchorPattern(u"<a id=\"([^\"]+)\"></a>"_s);
        for (const QcHandbookMarkdown::File &file : first) {
            const QString text = QString::fromUtf8(file.content);
            QSet<QString> &defined = anchors[file.name];
            for (QRegularExpressionMatchIterator it = anchorPattern.globalMatch(text); it.hasNext();) {
                const QString anchor = it.next().captured(1);
                QVERIFY2(!defined.contains(anchor), qPrintable(anchor));
                defined.insert(anchor);
            }
        }

        static const QRegularExpression linkPattern(u"\\]\\(([a-z]*\\.md)?(?:#([^)]+))?\\)"_s);
        qsizetype checked = 0;
        for (const QcHandbookMarkdown::File &file : first) {
            const QString text = QString::fromUtf8(file.content);
            for (QRegularExpressionMatchIterator it = linkPattern.globalMatch(text); it.hasNext();) {
                const QRegularExpressionMatch match = it.next();
                const QString target = match.captured(1).isEmpty() ? file.name : match.captured(1);
                QVERIFY2(anchors.contains(target), qPrintable(file.name + u": "_s + match.captured(0)));
                if (!match.captured(2).isEmpty()) {
                    QVERIFY2(anchors.value(target).contains(match.captured(2)), qPrintable(file.name + u": "_s + match.captured(0)));
                }
                ++checked;
            }
        }

        QVERIFY(checked > 2000);
        QCOMPARE(first.first().name, u"index.md"_s);
    }

    void committedHandbookIsUpToDate_data()
    {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QString>("directory");
        QTest::newRow("en") << u"en"_s << u"docs/handbook"_s;
        QTest::newRow("de") << u"de"_s << u"docs/handbook/de"_s;
    }

    void committedHandbookIsUpToDate()
    {
        // docs/handbook is `qtcraft-headless --no-mods --export-handbook docs/handbook` (and
        // `--language de` into docs/handbook/de); regenerate it when this fails.
        QFETCH(QString, language);
        QFETCH(QString, directory);

        const QDir docs(QDir(QStringLiteral(QTCRAFT_SOURCE_DIR)).filePath(directory));
        const QList<QcHandbookMarkdown::File> files =
            QcHandbookMarkdown::files(language == u"de"_s ? m_german : m_english);

        QStringList expectedNames;
        for (const QcHandbookMarkdown::File &file : files) {
            expectedNames.append(file.name);
            QFile committed(docs.filePath(file.name));
            QVERIFY2(committed.open(QIODevice::ReadOnly), qPrintable(committed.fileName()));
            QVERIFY2(committed.readAll() == file.content, qPrintable(u"%1 differs from the content"_s.arg(committed.fileName())));
        }

        QStringList present = docs.entryList({u"*.md"_s}, QDir::Files, QDir::Name);
        expectedNames.sort();
        QCOMPARE(present, expectedNames);
    }

    void jsonCarriesThePage()
    {
        const QcHandbookEntry *ore = m_english.entry(u"block:qtcraft:iron_ore"_s);
        const QJsonObject json = QcHandbookJson::entry(m_english, *ore);
        QCOMPARE(json.value(u"categoryTitle"_s).toString(), u"Blocks"_s);
        QCOMPARE(json.value(u"packName"_s).toString(), u"QtCraft"_s);
        QCOMPARE(json.value(u"facts"_s).toArray().size(), ore->facts.size());

        const QJsonObject recipe = json.value(u"sections"_s).toArray().first().toObject().value(u"recipes"_s).toArray().first().toObject();
        QCOMPARE(recipe.value(u"kind"_s).toString(), u"machine"_s);
        QCOMPARE(recipe.value(u"seconds"_s).toDouble(), 10.0);

        // Empty cells of a shaped recipe are null.
        const QcHandbookEntry *pickaxe = m_english.entry(u"item:qtcraft:wooden_pickaxe"_s);
        const QJsonArray cells = QcHandbookJson::entry(m_english, *pickaxe)
                                     .value(u"sections"_s).toArray().first().toObject()
                                     .value(u"recipes"_s).toArray().first().toObject()
                                     .value(u"cells"_s).toArray();
        QCOMPARE(cells.size(), 9);
        QVERIFY(cells[3].isNull());
        QCOMPARE(cells[4].toObject().value(u"target"_s).toString(), u"item:qtcraft:stick"_s);
        QCOMPARE(QcHandbookJson::categories(m_english).size(), 12);
    }

    void chapterFilesAreValidated()
    {
        const QcResult<QcHandbookChapter> chapter = QcContentParser::parseHandbookChapter(
            QJsonObject{{u"name"_s, u"tips"_s}, {u"sections"_s, QJsonArray{u"one"_s, u"two"_s}}, {u"related"_s, QJsonArray{u"stone"_s}}},
            u"mod"_s);
        QVERIFY2(chapter.has_value(), qcErrorText(chapter).constData());
        QCOMPARE(chapter->name, u"mod:tips"_s);
        QCOMPARE(chapter->titleKey, u"chapter.mod.tips"_s);
        QCOMPARE(chapter->related, QStringList{u"mod:stone"_s});
        QCOMPARE(chapter->order, 100);

        QVERIFY(!QcContentParser::parseHandbookChapter(QJsonObject{{u"name"_s, u"x"_s}}, u"mod"_s).has_value());
        QVERIFY(!QcContentParser::parseHandbookChapter(QJsonObject{{u"name"_s, u"x"_s}, {u"sections"_s, QJsonArray{u"Bad Name"_s}}}, u"mod"_s)
                     .has_value());
        QVERIFY(!QcContentParser::parseHandbookChapter(QJsonObject{{u"name"_s, u"x"_s}, {u"sections"_s, QJsonArray{u"a"_s, u"a"_s}}}, u"mod"_s)
                     .has_value());
        QVERIFY(!QcContentParser::parseHandbookChapter(
                     QJsonObject{{u"name"_s, u"x"_s}, {u"sections"_s, QJsonArray{u"a"_s}}, {u"related"_s, u"stone"_s}}, u"mod"_s)
                     .has_value());

        const QcResult<QcBlockDefinition> block =
            QcContentParser::parseBlock(QJsonObject{{u"name"_s, u"rock"_s}, {u"textures"_s, QJsonObject{{u"all"_s, u"rock"_s}}},
                                                    {u"description"_s, u"my.text"_s}},
                                        u"mod"_s);
        QVERIFY2(block.has_value(), qcErrorText(block).constData());
        QCOMPARE(block->descriptionKey, u"my.text"_s);

        const QcResult<QcItemDefinition> item = QcContentParser::parseItem(QJsonObject{{u"name"_s, u"gem"_s}}, u"mod"_s);
        QVERIFY2(item.has_value(), qcErrorText(item).constData());
        QCOMPARE(item->descriptionKey, u"item.mod.gem.description"_s);

        QVERIFY(!QcContentParser::parseBlock(QJsonObject{{u"name"_s, u"rock"_s}, {u"textures"_s, QJsonObject{{u"all"_s, u"rock"_s}}},
                                                         {u"description"_s, 3}},
                                             u"mod"_s)
                     .has_value());
    }
};

QTEST_GUILESS_MAIN(TestHandbook)
#include "test_handbook.moc"
