// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/loader/QcBaseContent.h"
#include "common/gameplay/QcKeyBindings.h"
#include "common/handbook/builder/QcHandbookBuilder.h"
#include "fixtures/QcTestUtil.h"
#include "server/signal/QcCircuitComponents.h"
#include "worlds/QcHandbookRules.h"

#include <QTest>

#include <algorithm>
#include <memory>

using namespace Qt::StringLiterals;
using Link = QcHandbookEntry::Link;
using Section = QcHandbookEntry::Section;

namespace {

std::shared_ptr<const QcContentRegistry> loadBase()
{
    QcResult<QcBaseContent::Loaded> loaded = QcTestUtil::baseContent();
    return loaded ? std::shared_ptr<const QcContentRegistry>(loaded->registry) : nullptr;
}

const Section *section(const QcHandbookEntry &entry, const QString &title)
{
    const auto it = std::ranges::find(entry.sections, title, &Section::title);
    return it == entry.sections.end() ? nullptr : &*it;
}

// The link to a block's page in a section, or null.
const Link *blockLink(const Section &section, const QString &block)
{
    const auto it = std::ranges::find(section.links, u"block:"_s + block, &Link::target);
    return it == section.links.end() ? nullptr : &*it;
}

QList<Link> allLinks(const QcHandbookEntry &entry)
{
    QList<Link> links;
    for (const Section &part : entry.sections) {
        links += part.links;
    }
    return links;
}

const QcBlockDefinition &block(const QcContentRegistry &content, const QString &name)
{
    return content.block(*content.blockId(name));
}

} // namespace

// The overviews built from the block data (decision E-685): signal components, transparent blocks,
// light sources and blast resistance.
class TestHandbookTables : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_base;
    QcHandbook m_english;
    QcHandbook m_german;

    QcHandbook build(const QString &language) const
    {
        return QcHandbookBuilder::build(*m_base, language, QcHandbookRules::values(*m_base),
                                        QcKeyBindings::defaults(QcKeyBindings::Platform::Other).placeholderKeys());
    }

private slots:
    void initTestCase()
    {
        m_base = loadBase();
        QVERIFY(m_base);
        m_english = build(u"en"_s);
        m_german = build(u"de"_s);
        QVERIFY2(m_english.problems().isEmpty(), qPrintable(m_english.problems().join(u'\n')));
        QVERIFY2(m_german.problems().isEmpty(), qPrintable(m_german.problems().join(u'\n')));
    }

    void tablesFollowTheOtherOverviews()
    {
        QStringList tables;
        for (const QcHandbookEntry &entry : m_english.entries()) {
            if (entry.category == u"tables"_s) {
                tables.append(entry.id);
            }
        }
        QCOMPARE(tables, (QStringList{u"table:fuels"_s, u"table:tools"_s, u"table:armour"_s, u"table:smelting"_s, u"table:signal"_s,
                                      u"table:transparent"_s, u"table:light"_s, u"table:blast"_s}));
    }

    void signalTableListsEveryComponentWithItsValues()
    {
        const QcHandbookEntry *table = m_english.entry(u"table:signal"_s);
        QVERIFY(table);
        QVERIFY(table->text.contains(u"20 per second"_s));

        // Every component with an item of its own, nothing else; the piston head belongs to the piston.
        int components = 0;
        for (const QcBlockDefinition &candidate : m_base->blocks()) {
            components += candidate.hasItem && candidate.signal.isComponent() ? 1 : 0;
        }
        QCOMPARE(allLinks(*table).size(), components);
        QVERIFY(std::ranges::none_of(allLinks(*table), [](const Link &link) { return link.target.endsWith(u"piston_head"_s); }));

        const Section *sources = section(*table, u"Sources"_s);
        const Section *wires = section(*table, u"Wire and diodes"_s);
        const Section *users = section(*table, u"Using power"_s);
        QVERIFY(sources && wires && users);
        QCOMPARE(table->sections.first().title, u"Sources"_s);
        QCOMPARE(blockLink(*sources, u"qtcraft:lever"_s)->note, u"gives 15 while switched on"_s);
        const int pulse = block(*m_base, u"qtcraft:stone_button"_s).signal.pulseTicks;
        QCOMPARE(blockLink(*sources, u"qtcraft:stone_button"_s)->note, u"gives 15 for %1 ticks when pressed"_s.arg(pulse));
        QCOMPARE(blockLink(*sources, u"qtcraft:detector_rail"_s)->note, u"gives 15 while a minecart stands on it"_s);
        QVERIFY(blockLink(*sources, u"qtcraft:signal_observer"_s)->note.endsWith(u"; passes no power through"_s));

        const QString repeater = blockLink(*wires, u"qtcraft:signal_repeater"_s)->note;
        QVERIFY2(repeater.contains(u"1 to 4 steps of %1 ticks"_s.arg(QcCircuitComponents::repeaterStepTicks)), qPrintable(repeater));
        QVERIFY(blockLink(*wires, u"qtcraft:signal_dust"_s)->note.contains(u"15 at the source"_s));

        const int pushLimit = block(*m_base, u"qtcraft:sticky_signal_piston"_s).signal.pushLimit;
        QCOMPARE(blockLink(*users, u"qtcraft:sticky_signal_piston"_s)->note,
                 u"pushes up to %1 blocks while powered and pulls one back; passes no power through"_s.arg(pushLimit));
        QVERIFY(blockLink(*users, u"qtcraft:oak_door"_s));
        QVERIFY(blockLink(*users, u"qtcraft:explosive"_s));
        QVERIFY(blockLink(*users, u"qtcraft:signal_dropper"_s)->note.contains(u"throws one item after %1 ticks"_s.arg(QcCircuitComponents::dispenserDelayTicks)));
    }

    void transparentTableShowsLightAndSpawning()
    {
        const QcHandbookEntry *table = m_english.entry(u"table:transparent"_s);
        QVERIFY(table);
        const Section *solid = section(*table, u"Solid: creatures collide with them"_s);
        const Section *passable = section(*table, u"Not solid: creatures pass through"_s);
        const Section *liquids = section(*table, u"Liquids"_s);
        QVERIFY(solid && passable && liquids);

        // Glass and ice are drawn see-through: no ground for spawns (QcSpawnGround, decision E-836).
        QCOMPARE(blockLink(*solid, u"qtcraft:glass"_s)->note, u"light passes, no creatures spawn on it"_s);
        QCOMPARE(blockLink(*solid, u"qtcraft:ice"_s)->note, u"dims light by %1, no creatures spawn on it"_s.arg(block(*m_base, u"qtcraft:ice"_s).lightOpacity));
        QCOMPARE(blockLink(*passable, u"qtcraft:tall_grass"_s)->note, u"light passes, creatures spawn inside it"_s);
        // A cobweb slows bodies, so nothing spawns in it (QcNaturalSpawner).
        QCOMPARE(blockLink(*passable, u"qtcraft:cobweb"_s)->note, u"light passes, no creatures spawn on or in it"_s);
        QVERIFY(blockLink(*liquids, u"qtcraft:water"_s)->note.endsWith(u"water creatures spawn in it"_s));
        QVERIFY(blockLink(*liquids, u"qtcraft:lava"_s)->note.endsWith(u"no creatures spawn on or in it"_s));

        // Only blocks that are no opaque full cubes, without building parts.
        const QList<Link> links = allLinks(*table);
        for (const Link &link : links) {
            const QcBlockDefinition &listed = block(*m_base, link.target.mid(6));
            QVERIFY2(!listed.opaque && listed.partOf.isEmpty(), qPrintable(link.target));
        }
        QVERIFY(!blockLink(*solid, u"qtcraft:stone"_s));
    }

    void lightTableIsBrightestFirst()
    {
        const QcHandbookEntry *table = m_english.entry(u"table:light"_s);
        QVERIFY(table);
        const QList<Link> sources = allLinks(*table);

        int expected = 0;
        for (const QcBlockDefinition &candidate : m_base->blocks()) {
            expected += candidate.renderLayer != QcRenderLayer::Invisible && candidate.lightLevel > 0 ? 1 : 0;
        }
        QCOMPARE(sources.size(), expected);

        int previous = QcBlockDefinition::maxLightLevel;
        for (const Link &link : sources) {
            const int level = block(*m_base, link.target.mid(6)).lightLevel;
            QCOMPARE(link.note, u"%1 of 15"_s.arg(level));
            QVERIFY(level <= previous);
            previous = level;
        }
        QVERIFY(std::ranges::find(sources, u"block:qtcraft:torch"_s, &Link::target) != sources.end());
    }

    void blastTableUsesTheEffectiveResistance()
    {
        const QcHandbookEntry *table = m_english.entry(u"table:blast"_s);
        QVERIFY(table);
        const Section *proof = section(*table, u"Never break in explosions"_s);
        const Section *resisting = section(*table, u"Resistance, strongest first"_s);
        QVERIFY(proof && resisting);

        QVERIFY(blockLink(*proof, u"qtcraft:bedrock"_s));
        QCOMPARE(blockLink(*resisting, u"qtcraft:obsidian"_s)->note, u"1200"_s);
        QCOMPARE(blockLink(*resisting, u"qtcraft:stone"_s)->note, u"6"_s);
        QCOMPARE(blockLink(*resisting, u"qtcraft:water"_s)->note, u"100, liquid"_s);
        // The furnace has no value of its own and resists as much as its hardness.
        const double furnace = block(*m_base, u"qtcraft:furnace"_s).hardness;
        QCOMPARE(blockLink(*resisting, u"qtcraft:furnace"_s)->note, u"%1, from hardness"_s.arg(furnace));

        double previous = QcBlastDefinition::unbreakable;
        for (const Link &link : resisting->links) {
            const QcBlockDefinition &listed = block(*m_base, link.target.mid(6));
            const double resistance = listed.blast.effectiveResistance(listed.hardness, listed.liquid);
            QVERIFY2(resistance <= previous, qPrintable(link.target));
            previous = resistance;
        }
    }

    void germanTablesAreTranslated()
    {
        for (const QString &id : {u"table:signal"_s, u"table:transparent"_s, u"table:light"_s, u"table:blast"_s}) {
            const QcHandbookEntry *german = m_german.entry(id);
            const QcHandbookEntry *english = m_english.entry(id);
            QVERIFY(german && english);
            QVERIFY(german->title != english->title);
            QVERIFY(german->text != english->text);
            QCOMPARE(german->englishTitle, english->title);
            for (const Section &part : german->sections) {
                for (const Link &link : part.links) {
                    QVERIFY2(!link.note.contains(u'{') && !link.note.contains(u'%') && !link.note.contains(u"handbook."_s),
                             qPrintable(id + u": "_s + link.note));
                }
            }
        }
        QCOMPARE(m_german.entry(u"table:signal"_s)->title, u"Signalbauteile"_s);
        QCOMPARE(blockLink(m_german.entry(u"table:signal"_s)->sections.first(), u"qtcraft:lever"_s)->note,
                 u"gibt 15, solange er eingeschaltet ist"_s);
    }
};

QTEST_GUILESS_MAIN(TestHandbookTables)
#include "test_handbook_tables.moc"
