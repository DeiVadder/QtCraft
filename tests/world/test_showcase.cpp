#include "QcTestUtil.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worldgen/showcase/QcShowcaseBlockArea.h"
#include "worldgen/showcase/QcShowcaseCategories.h"
#include "worldgen/showcase/QcShowcaseGenerator.h"

#include <QSet>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

// Every chunk any placement of the plan touches, generated once.
QHash<QcChunkPos, std::shared_ptr<QcChunk>> generateAll(const QcShowcaseGenerator &generator)
{
    QHash<QcChunkPos, std::shared_ptr<QcChunk>> chunks;
    for (auto it = generator.plan().placementsByChunk.constBegin(); it != generator.plan().placementsByChunk.constEnd(); ++it) {
        chunks.insert(it.key(), generator.generate(it.key()));
    }
    return chunks;
}

QcBlockId blockAt(const QHash<QcChunkPos, std::shared_ptr<QcChunk>> &chunks, QcBlockPos pos)
{
    const std::shared_ptr<QcChunk> chunk = chunks.value(pos.chunk());
    return chunk ? chunk->block(pos.localX(), pos.y, pos.localZ()) : qcAirBlockId;
}

// Every state in the generated chunks with the number of cells it fills.
QHash<QcBlockId, int> statesIn(const QHash<QcChunkPos, std::shared_ptr<QcChunk>> &chunks, int fromY, int toY)
{
    QHash<QcBlockId, int> states;
    for (const std::shared_ptr<QcChunk> &chunk : chunks) {
        for (int y = fromY; y <= toY; ++y) {
            for (int z = 0; z < QcWorld::chunkSize; ++z) {
                for (int x = 0; x < QcWorld::chunkSize; ++x) {
                    states[chunk->block(x, y, z)] += 1;
                }
            }
        }
    }
    return states;
}

} // namespace

class TestShowcase : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<QcContentRegistry> m_content;
    std::unique_ptr<QcShowcaseGenerator> m_generator;
    QHash<QcChunkPos, std::shared_ptr<QcChunk>> m_chunks;

private slots:
    void initTestCase()
    {
        m_content = QcTestUtil::loadContent();
        m_generator = std::make_unique<QcShowcaseGenerator>(m_content);
        QVERIFY2(m_generator->warnings().isEmpty(), qPrintable(m_generator->warnings().join(u'\n')));
        m_chunks = generateAll(*m_generator);
    }

    void factoryKnowsTheShowcase()
    {
        QVERIFY(QcWorldGenerators::isKnown(u"showcase"_s));
        QVERIFY(QcWorldGenerators::isKnown(u"terrain"_s));
        QVERIFY(QcWorldGenerators::isKnown(u"flat"_s) && QcWorldGenerators::isKnown(u"void"_s));
        QVERIFY(!QcWorldGenerators::isKnown(u"spiral"_s));
        QVERIFY(dynamic_cast<QcShowcaseGenerator *>(QcWorldGenerators::create(u"showcase"_s, m_content, 1).get()));
        QVERIFY(!QcWorldGenerators::create(u"spiral"_s, m_content, 1));
    }

    void groundIsFlatOutsideTheAreas()
    {
        const QcShowcasePlan &plan = m_generator->plan();
        QCOMPARE(plan.groundTop(), 63);
        // Far away from all areas: only the layers, grass on top.
        const std::shared_ptr<QcChunk> far = m_generator->generate({100, -100});
        for (int x = 0; x < QcWorld::chunkSize; ++x) {
            QCOMPARE(far->highestNonAir(x, 7), 63);
        }
        QCOMPARE(far->block(3, 63, 3), *m_content->blockId(u"qtcraft:grass_block"_s));
        QCOMPARE(far->block(3, 0, 3), *m_content->blockId(u"qtcraft:bedrock"_s));
        QVERIFY(m_generator->isNaturalGround(*m_content->blockId(u"qtcraft:grass_block"_s)));
        QVERIFY(!m_generator->isNaturalGround(*m_content->blockId(u"qtcraft:polished_stone"_s)));
        QCOMPARE(m_generator->spawnPoint(), (QcBlockPos{0, 64, 0}));
    }

    void isDeterministicAndIgnoresTheSeed()
    {
        const auto other = QcWorldGenerators::create(u"showcase"_s, m_content, 987654321);
        for (auto it = m_chunks.constBegin(); it != m_chunks.constEnd(); ++it) {
            const std::shared_ptr<QcChunk> again = other->generate(it.key());
            for (int y = 60; y < 90; ++y) {
                for (int z = 0; z < QcWorld::chunkSize; ++z) {
                    for (int x = 0; x < QcWorld::chunkSize; ++x) {
                        QCOMPARE(again->block(x, y, z), it.value()->block(x, y, z));
                    }
                }
            }
            QCOMPARE(other->labelsIn(it.key()), m_generator->labelsIn(it.key()));
        }
    }

    void everyStateOfEveryBlockIsOnShow()
    {
        const QcContentRegistry &content = *m_content;
        const QHash<QcBlockId, int> shown = statesIn(m_chunks, 64, 70);
        int expectedStates = 0;
        for (qsizetype type = 0; type < content.blocks().size(); ++type) {
            const QcBlockDefinition &block = content.blocks()[type];
            if (block.renderLayer == QcRenderLayer::Invisible) {
                continue;
            }

            // Every state, doors with both halves (the upper halves stand on the lower ones); of blocks
            // with more than 32 states those that differ from the default in one property.
            const QcBlockStateTable &table = content.stateTable();
            const QcBlockId first = table.firstState(quint32(type));
            for (int index = 0; index < table.stateCount(quint32(type)); ++index) {
                const QcBlockId state = first + quint32(index);
                const auto differs = [&](const QcBlockProperty &property) {
                    return content.property(state, property.name) != content.property(first, property.name);
                };
                if (table.stateCount(quint32(type)) > 32 && std::ranges::count_if(block.properties, differs) > 1) {
                    continue;
                }
                ++expectedStates;
                QVERIFY2(shown.contains(state), qPrintable(content.stateName(state)));
            }
        }
        QVERIFY2(expectedStates > 1000, qPrintable(QString::number(expectedStates)));
    }

    void podiumsKeepOffChunkBorders()
    {
        // Labels of podiums sit over their podium: none in a column on a chunk border, so the
        // border checks of a loading neighbour never touch a block on show.
        const QcShowcasePlan &plan = m_generator->plan();
        int podiumLabels = 0;
        for (const QcShowcaseAreaInfo &area : plan.areas) {
            if (!area.name.startsWith(u"blocks-"_s)) {
                continue;
            }
            for (const QList<QcWorldLabel> &labels : plan.labelsByChunk) {
                for (const QcWorldLabel &label : labels) {
                    const int x = int(std::floor(label.position.x));
                    const int z = int(std::floor(label.position.z));
                    const bool inArea = x >= area.origin.x && x < area.origin.x + area.sizeX && z >= area.origin.z
                                        && z < area.origin.z + area.sizeZ;
                    if (!inArea || label.heading) {
                        continue;
                    }
                    ++podiumLabels;
                    QVERIFY2(QcShowcaseBlockArea::usableX(x) && QcShowcaseBlockArea::usableZ(z),
                             qPrintable(u"%1 at %2, %3"_s.arg(label.text).arg(x).arg(z)));
                }
            }
        }
        QVERIFY(podiumLabels > 900);
        QVERIFY(QcShowcaseBlockArea::usableX(1) && QcShowcaseBlockArea::usableX(13) && QcShowcaseBlockArea::usableX(-15));
        QVERIFY(!QcShowcaseBlockArea::usableX(0) && !QcShowcaseBlockArea::usableX(15) && !QcShowcaseBlockArea::usableX(-1));
        QVERIFY(QcShowcaseBlockArea::usableZ(4) && QcShowcaseBlockArea::usableZ(-3) && !QcShowcaseBlockArea::usableZ(-2));
    }

    void doorsShowBothHalvesAndHangingBlocksTheirHold()
    {
        const QcContentRegistry &content = *m_content;
        const QcBlockId lower = *content.stateId(u"qtcraft:oak_door[facing=east,half=bottom,open=true,hinge=left]"_s);
        const QcBlockId upper = content.withProperty(lower, u"half"_s, u"top"_s);
        const QcBlockId torch = *content.stateId(u"qtcraft:torch[attach=west]"_s);
        const QcBlockId lantern = *content.stateId(u"qtcraft:lantern[attach=ceiling]"_s);
        int checked = 0;
        for (auto it = m_chunks.constBegin(); it != m_chunks.constEnd(); ++it) {
            for (int y = 64; y < 70; ++y) {
                for (int z = 0; z < QcWorld::chunkSize; ++z) {
                    for (int x = 0; x < QcWorld::chunkSize; ++x) {
                        const QcBlockId block = it.value()->block(x, y, z);
                        const QcBlockPos pos{it.key().x * 16 + x, y, it.key().z * 16 + z};
                        if (block == lower) {
                            QCOMPARE(blockAt(m_chunks, pos.offset(0, 1, 0)), upper);
                            ++checked;
                        } else if (block == torch) {
                            QVERIFY(content.block(blockAt(m_chunks, pos.offset(-1, 0, 0))).solid);
                            ++checked;
                        } else if (block == lantern) {
                            QVERIFY(content.block(blockAt(m_chunks, pos.offset(0, 1, 0))).solid);
                            ++checked;
                        }
                    }
                }
            }
        }
        QVERIFY2(checked >= 3, qPrintable(QString::number(checked)));
    }

    void blocksAreSortedIntoCategories()
    {
        const QcContentRegistry &content = *m_content;
        const auto categoryOf = [&content](const char *name) {
            const QcBlockDefinition &block = content.block(*content.blockId(QString::fromLatin1(name)));
            return content.worldgen().showcase().categories[QcShowcaseCategories::categoryOf(block, content)].name;
        };
        QCOMPARE(categoryOf("qtcraft:water"), u"liquids"_s);
        QCOMPARE(categoryOf("qtcraft:oak_stairs"), u"parts"_s);
        QCOMPARE(categoryOf("qtcraft:torch"), u"light"_s);
        QCOMPARE(categoryOf("qtcraft:poppy"), u"plants"_s);
        QCOMPARE(categoryOf("qtcraft:furnace"), u"functional"_s);
        QCOMPARE(categoryOf("qtcraft:oak_log"), u"wood"_s);
        QCOMPARE(categoryOf("qtcraft:iron_ore"), u"ores"_s);
        QCOMPARE(categoryOf("qtcraft:red_wool"), u"colours"_s);
        QCOMPARE(categoryOf("qtcraft:glass"), u"glass"_s);
        QCOMPARE(categoryOf("qtcraft:sand"), u"earth"_s);
        QCOMPARE(categoryOf("qtcraft:stone"), u"stone"_s);

        // Every visible block in exactly one category.
        const QList<QList<quint32>> sorted = QcShowcaseCategories::sort(content);
        qsizetype total = 0;
        for (const QList<quint32> &types : sorted) {
            total += types.size();
        }
        QCOMPARE(total, content.blockCount() - 1);
    }

    void creaturesItemsAndRulesAreSetUp()
    {
        const QcWorldSetup setup = m_generator->setup();
        QCOMPARE(setup.timeOfDay, std::optional(150.0));
        QVERIFY(!setup.daylightCycle);
        QVERIFY(!setup.mobSpawning);
        // Every crop stage and soil state on show stays (decision E-295).
        QVERIFY(!setup.randomTicks);
        // The sky stays clear (decision E-395).
        QVERIFY(!setup.weatherCycle);
        // The settlement's farmers harvest and pick up bread; every other creature on show is
        // confined, so it changes nothing and its pen holds it, also one that teleports (decisions
        // E-460, E-510). Only the settlement's two villagers and the pets, which belong to someone,
        // are free; one mannequin and the Kjut parent (its children orbit it, decision E-839) stand
        // without a mind.
        QVERIFY(setup.mobGriefing);

        QSet<QString> mobs;
        int free = 0;
        int withoutMind = 0;
        for (const QcWorldSetup::Mob &mob : setup.mobs) {
            mobs.insert(mob.mob);
            if (!mob.confined && mob.owner.isEmpty()) {
                QCOMPARE(mob.mob, u"qtcraft:villager"_s);
                ++free;
            }
            withoutMind += mob.noAi ? 1 : 0;
            // Every creature stands in free space on the ground.
            const QcBlockPos feet{int(std::floor(mob.at.x)), int(std::floor(mob.at.y + 0.1)) + 0, int(std::floor(mob.at.z))};
            QVERIFY2(!m_content->block(blockAt(m_chunks, feet.offset(0, 1, 0))).solid, qPrintable(mob.mob));
        }
        for (const QcMobDefinition &mob : m_content->mobs()) {
            QVERIFY2(mobs.contains(mob.name), qPrintable(mob.name));
        }
        QCOMPARE(free, 2);
        QCOMPARE(withoutMind, 2);

        QSet<QString> items;
        for (const QcWorldSetup::Contents &contents : setup.contents) {
            const QcBlockDefinition &block = m_content->block(blockAt(m_chunks, contents.at));
            // Contents at a rail go into the container cart standing there (decision E-386), at a
            // campfire onto the fire (decision E-772).
            const bool cart = std::ranges::any_of(setup.minecarts, [&contents](const QcWorldSetup::Cart &placed) {
                return placed.at == contents.at && placed.item != u"qtcraft:minecart"_s;
            });
            QVERIFY2(cart || block.containerSlots >= contents.items.size() || block.machine.isMachine() || block.campfire.slotCount >= contents.items.size(),
                     qPrintable(block.name));
            for (const QcStructureDefinition::Stack &stack : contents.items) {
                items.insert(stack.item);
            }
        }
        for (const QcItemDefinition &item : m_content->items()) {
            QVERIFY2(items.contains(item.name), qPrintable(item.name));
        }

        // The liquid sources of the demos start flowing, the floating sand falls.
        QVERIFY(setup.updates.size() >= 6);
        for (const QcBlockPos &pos : setup.updates) {
            QVERIFY(blockAt(m_chunks, pos) != qcAirBlockId);
        }
        QVERIFY(!setup.growing.isEmpty());
        for (const QcBlockPos &pos : setup.growing) {
            QVERIFY(m_content->block(blockAt(m_chunks, pos)).sapling.isSapling());
        }
    }

    void hostilePensAreThickerThanTheReach()
    {
        // A mod creature like the zombie but with a long reach.
        const std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
        QcMobDefinition lancer = content->mobs().at(*content->mobIndex(u"qtcraft:zombie"_s));
        lancer.name = u"test:lancer"_s;
        lancer.attackRange = 4.5;
        content->registerMob(lancer);
        const QcShowcaseGenerator generator(content);
        const QHash<QcChunkPos, std::shared_ptr<QcChunk>> chunks = generateAll(generator);
        const QcBlockId glass = *content->blockId(content->worldgen().showcase().enclosure);

        // From where each hostile creature stands, the glass in every direction is thicker than what
        // its reach (from its centre) leaves once it presses against the glass.
        int checked = 0;
        constexpr int maxWalk = 16;
        const QcWorldSetup setup = generator.setup();
        for (const QcWorldSetup::Mob &placed : setup.mobs) {
            const QcMobDefinition &mob = content->mobs().at(*content->mobIndex(placed.mob));
            // Only creatures with blows reach through glass (the witch only throws).
            if (!mob.hostile || mob.attackDamage <= 0.0) {
                continue;
            }
            const QcBlockPos start{int(std::floor(placed.at.x)), int(std::floor(placed.at.y)), int(std::floor(placed.at.z))};
            for (const auto &[dx, dz] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                // Pens built from data may have walls of other blocks (the spider cave's stone): only
                // glass within reach of the creature counts here, and the walk ends at the first solid
                // block (not through a stone wall to the glass of the next exhibit).
                QcBlockPos pos = start;
                for (int step = 0; step < maxWalk && !content->block(blockAt(chunks, pos)).solid; ++step) {
                    pos = pos.offset(dx, 0, dz);
                }
                if (blockAt(chunks, pos) != glass) {
                    continue;
                }
                int thickness = 0;
                for (; blockAt(chunks, pos) == glass; pos = pos.offset(dx, 0, dz)) {
                    ++thickness;
                }
                QVERIFY2(thickness > mob.attackRange - mob.width / 2.0, qPrintable(u"%1: %2"_s.arg(mob.name).arg(thickness)));
                ++checked;
            }
        }
        QVERIFY(checked >= 8);
    }

    void undeadStandInShade()
    {
        // The showcase stands at noon: creatures that burn in daylight (decision E-426) stand under
        // an opaque roof, which glass is not.
        const std::shared_ptr<QcContentRegistry> content = QcTestUtil::loadContent();
        const QcShowcaseGenerator generator(content);
        const QHash<QcChunkPos, std::shared_ptr<QcChunk>> chunks = generateAll(generator);
        int checked = 0;
        const QcWorldSetup setup = generator.setup();
        for (const QcWorldSetup::Mob &placed : setup.mobs) {
            const QcMobDefinition &mob = content->mobs().at(*content->mobIndex(placed.mob));
            if (!mob.burnsInDaylight) {
                continue;
            }
            bool roofed = false;
            for (int y = int(std::floor(placed.at.y + mob.height)); y < QcWorld::worldHeight && !roofed; ++y) {
                const QcBlockId block = blockAt(chunks, {int(std::floor(placed.at.x)), y, int(std::floor(placed.at.z))});
                roofed = block != qcAirBlockId && content->block(block).opaque;
            }
            QVERIFY2(roofed, qPrintable(mob.name));
            ++checked;
        }
        QVERIFY(checked >= 2);
    }

    void labelsNameTheBlocksAndLieInTheirChunk()
    {
        const QcShowcasePlan &plan = m_generator->plan();
        QSet<QString> texts;
        qsizetype headings = 0;
        for (auto it = plan.labelsByChunk.constBegin(); it != plan.labelsByChunk.constEnd(); ++it) {
            for (const QcWorldLabel &label : it.value()) {
                QCOMPARE((QcBlockPos{int(std::floor(label.position.x)), 0, int(std::floor(label.position.z))}.chunk()), it.key());
                QVERIFY2(m_content->translations().has(label.text, u"en"_s) || label.text.isEmpty(), qPrintable(label.text));
                QVERIFY(label.text.size() <= QcWorldLabel::maxTextLength);
                texts.insert(label.text);
                headings += label.heading ? 1 : 0;
            }
            QVERIFY(it.value().size() <= QcWorldLabel::maxPerChunk);
            QCOMPARE(m_generator->labelsIn(it.key()), it.value());
        }
        QCOMPARE(headings, plan.areas.size());
        QVERIFY(texts.contains(u"block.qtcraft.oak_stairs"_s));
        QVERIFY(texts.contains(u"mob.qtcraft.zombie"_s));
        QVERIFY(texts.contains(u"showcase.category.stone"_s));
        QVERIFY(plan.labelCount() > 1000);
    }

    void areasDoNotOverlapAndHaveTourStops()
    {
        const QcShowcasePlan &plan = m_generator->plan();
        QVERIFY(plan.areas.size() >= 18);
        for (qsizetype i = 0; i < plan.areas.size(); ++i) {
            const QcShowcaseAreaInfo &a = plan.areas[i];
            QVERIFY(a.origin.z + a.sizeZ <= -5);
            for (qsizetype j = i + 1; j < plan.areas.size(); ++j) {
                const QcShowcaseAreaInfo &b = plan.areas[j];
                const bool apart = a.origin.x + a.sizeX <= b.origin.x || b.origin.x + b.sizeX <= a.origin.x || a.origin.z + a.sizeZ <= b.origin.z
                                   || b.origin.z + b.sizeZ <= a.origin.z;
                QVERIFY2(apart, qPrintable(a.name + u" / "_s + b.name));
            }
        }

        // Two stops per area: from its lookout (standing on the platform) and close by; a station
        // seen from the front (decision E-839) has both on the ground.
        QSet<QString> fromFront;
        for (const QcStructureDefinition &structure : m_content->worldgen().structures()) {
            if (structure.showcaseFromFront) {
                fromFront.insert(u"demo-"_s + structure.name.mid(structure.name.indexOf(u':') + 1));
            }
        }
        QVERIFY(fromFront.contains(u"demo-kjut_family"_s));
        QCOMPARE(plan.tour.size(), 2 * plan.areas.size());
        for (const QcShowcaseTourStop &stop : plan.tour) {
            const QcChunkPos chunk = QcBlockPos{stop.x, 0, stop.z}.chunk();
            const std::shared_ptr<QcChunk> generated = m_generator->generate(chunk);
            const int top = generated->highestNonAir(QcBlockPos{stop.x, 0, stop.z}.localX(), QcBlockPos{stop.x, 0, stop.z}.localZ());
            QVERIFY2(stop.name.endsWith(u"-near"_s) || fromFront.contains(stop.name) ? top == 63 : top > 63, qPrintable(stop.name));
            QVERIFY(stop.pitch < 0.0f && stop.pitch > -60.0f);
            QVERIFY(stop.yaw >= 0.0f && stop.yaw < 360.0f);
        }
    }

    void fireStationBurnsOnMagmaBehindGlass()
    {
        // The fire pit (decision E-393): fire on magma only (eternal, spread is off anyway) behind one row of glass, nothing
        // flammable right next to any fire, and the hay bale on show.
        const auto station = std::ranges::find(m_generator->plan().areas, u"demo-fire_station"_s, &QcShowcaseAreaInfo::name);
        QVERIFY(station != m_generator->plan().areas.end());
        const QcBlockId fire = *m_content->blockId(u"qtcraft:fire"_s);
        int fires = 0;
        bool hay = false;
        for (int x = 0; x < station->sizeX; ++x) {
            for (int y = 0; y < station->sizeY; ++y) {
                for (int z = 0; z < station->sizeZ; ++z) {
                    const QcBlockPos pos = station->origin.offset(x, y, z);
                    hay = hay || blockAt(m_chunks, pos) == *m_content->blockId(u"qtcraft:hay_block"_s);
                    if (blockAt(m_chunks, pos) != fire) {
                        continue;
                    }
                    ++fires;
                    QVERIFY(m_content->block(blockAt(m_chunks, pos.offset(0, -1, 0))).fire.eternal);
                    for (const QcBlockFace face : qcAllBlockFaces) {
                        QVERIFY(!m_content->block(blockAt(m_chunks, qcNeighbour(pos, face))).fire.isFlammable());
                    }
                }
            }
        }
        QCOMPARE(fires, 9);
        QVERIFY(hay);
    }

    void modBlocksAppearWithoutExtraData()
    {
        // A copy of the base game plus one block of a mod with two states.
        auto content = std::make_shared<QcContentRegistry>(*m_content);
        QcBlockDefinition crystal;
        crystal.name = u"gems:crystal"_s;
        crystal.translationKey = u"block.gems.crystal"_s;
        crystal.tool = u"pickaxe"_s;
        crystal.properties = {{u"glowing"_s, {u"false"_s, u"true"_s}}};
        crystal.faceTextures.fill(u"qtcraft:stone"_s);
        QVERIFY(content->registerBlock(crystal));

        const QcShowcaseGenerator generator(content);
        const QHash<QcBlockId, int> shown = statesIn(generateAll(generator), 64, 70);
        QVERIFY(shown.contains(*content->stateId(u"gems:crystal[glowing=false]"_s)));
        QVERIFY(shown.contains(*content->stateId(u"gems:crystal[glowing=true]"_s)));

        bool labelled = false;
        for (const QList<QcWorldLabel> &labels : generator.plan().labelsByChunk) {
            for (const QcWorldLabel &label : labels) {
                labelled = labelled || (label.text == u"block.gems.crystal"_s && label.detail == u"glowing=true"_s);
            }
        }
        QVERIFY(labelled);
    }
};

QTEST_GUILESS_MAIN(TestShowcase)
#include "test_showcase.moc"
