// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/blockbehaviour/QcHangingParser.h"
#include "common/content/worldgen/QcStructureParser.h"
#include "common/protocol/QcProtocol.h"
#include "fixtures/QcMachineFixture.h"
#include "fixtures/QcTestUtil.h"
#include "server/gameplay/QcServerCombat.h"
#include "server/hanging/QcHangingRules.h"
#include "server/storage/QcEntityCodec.h"

#include <QJsonDocument>
#include <QTest>

using namespace Qt::StringLiterals;

// Item frames and paintings (decisions E-790 to E-797): placing them on block faces, putting an
// item into a frame and turning it, hits, support loss, the comparator reading a frame, saving,
// the protocol and the textures of large motifs.
class TestHanging : public QcMachineFixture
{
    Q_OBJECT

    static constexpr int cooldownTicks = static_cast<int>(QcServerCombat::playerAttackCooldown * QcPhysics::tickRate) + 1;

    QcHangingManager &hangings() { return m_server->entities().hangings(); }

    // Gives the player `count` of `name` and holds it.
    void hold(const QString &name, int count = 1)
    {
        chat(u"/give %1 %2"_s.arg(name).arg(count));
        send(QcMsg::SelectHotbarSlot{slotOf(item(qPrintable(u"qtcraft:"_s + name)))});
    }

    // Clicks `face` of the block at `target` with the held item; returns the newest frame or painting.
    const QcHangingEntity *useOn(QcBlockPos target, QcBlockFace face)
    {
        const qsizetype before = hangings().all().size();
        send(QcMsg::UseItem{target, face, {0.5, 0.5, 0.5}});
        return hangings().all().size() > before ? &hangings().all().constLast() : nullptr;
    }

    // A stone wall north of the player, `width` wide (centred) and `height` high from the stage.
    void buildWall(int width, int height)
    {
        for (int dx = -(width - 1) / 2; dx <= width / 2; ++dx) {
            for (int dy = 0; dy < height; ++dy) {
                m_server->world().setBlock(m_feet.offset(dx, dy, -2), id("qtcraft:stone"));
            }
        }
    }

    // Sets a block the way the server changes blocks (neighbour updates, frames and paintings check).
    void setBlock(QcBlockPos pos, const QString &name)
    {
        chat(u"/setblock %1 %2 %3 %4"_s.arg(pos.x).arg(pos.y).arg(pos.z).arg(name));
    }

    // A frame record as a save would hold it.
    QcHangingEntity frame(QcBlockPos cell, QcBlockFace face, const char *held, int rotation)
    {
        QcHangingEntity hanging;
        hanging.item = u"qtcraft:item_frame"_s;
        hanging.hanging.face = face;
        hanging.hanging.cell = cell;
        hanging.hanging.rotation = static_cast<quint8>(rotation);
        hanging.held = held ? QcItemStack{item(held), 1, 0} : QcItemStack();
        hanging.motion.position = hanging.hanging.centre();
        return hanging;
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void framesHangOnEveryFaceAndOnlyOncePerFace()
    {
        join();
        buildWall(1, 1);
        hold(u"item_frame"_s, 3);
        const QcHangingEntity *onWall = useOn(m_feet.offset(0, 0, -2), QcBlockFace::South);
        QVERIFY(onWall);
        QCOMPARE(onWall->hanging.type, QcHangingType::ItemFrame);
        QCOMPARE(onWall->hanging.cell, m_feet.offset(0, 0, -1));
        QCOMPARE(onWall->item, u"qtcraft:item_frame"_s);
        QCOMPARE(countOf(item("qtcraft:item_frame")), 2);

        // The same face again is refused; the floor in front takes one.
        QVERIFY(!useOn(m_feet.offset(0, 0, -2), QcBlockFace::South));
        const QcHangingEntity *onFloor = useOn(m_feet.offset(1, -1, -1), QcBlockFace::Up);
        QVERIFY(onFloor);
        QCOMPARE(onFloor->hanging.face, QcBlockFace::Up);
        QCOMPARE(countOf(item("qtcraft:item_frame")), 1);

        // Not into a cell with a block.
        m_server->world().setBlock(m_feet.offset(-1, 0, -1), id("qtcraft:stone"));
        QVERIFY(!useOn(m_feet.offset(-1, -1, -1), QcBlockFace::Up));
    }

    void rightClicksPutAnItemInAndTurnItHitsTakeItOut()
    {
        join();
        buildWall(1, 1);
        hold(u"item_frame"_s);
        const quint32 id = useOn(m_feet.offset(0, 0, -2), QcBlockFace::South)->id;
        hold(u"apple"_s, 2);
        send(QcMsg::UseEntity{id});
        QCOMPARE(hangings().find(id)->held.item, item("qtcraft:apple"));
        QCOMPARE(hangings().find(id)->held.count, 1);
        QCOMPARE(countOf(item("qtcraft:apple")), 1);

        // Each further click turns it an eighth; eight clicks come round again.
        for (int click = 1; click <= QcHanging::rotations; ++click) {
            send(QcMsg::UseEntity{id});
            QCOMPARE(int(hangings().find(id)->hanging.rotation), click % QcHanging::rotations);
        }
        QCOMPARE(countOf(item("qtcraft:apple")), 1);

        // The first hit drops the item, the next the frame.
        ticks(cooldownTicks);
        send(QcMsg::AttackEntity{id});
        QVERIFY(hangings().find(id) && hangings().find(id)->held.isEmpty());
        QCOMPARE(droppedCount(item("qtcraft:apple")), 1);
        ticks(cooldownTicks);
        send(QcMsg::AttackEntity{id});
        QVERIFY(!hangings().find(id));
        QCOMPARE(droppedCount(item("qtcraft:item_frame")), 1);
    }

    void framesFallOffWhenTheirBlockGoesOrTheirCellFills()
    {
        join();
        buildWall(3, 1);
        hold(u"item_frame"_s, 2);
        const quint32 first = useOn(m_feet.offset(0, 0, -2), QcBlockFace::South)->id;
        const quint32 second = useOn(m_feet.offset(1, 0, -2), QcBlockFace::South)->id;
        hold(u"apple"_s);
        send(QcMsg::UseEntity{first});

        // A block change next to a frame makes it check at once.
        setBlock(m_feet.offset(0, 0, -2), u"air"_s);
        ticks(1);
        QVERIFY(!hangings().find(first));
        QCOMPARE(droppedCount(item("qtcraft:item_frame")), 1);
        QCOMPARE(droppedCount(item("qtcraft:apple")), 1);

        // A change behind the server's back is found by the round of checks.
        m_server->world().setBlock(m_feet.offset(1, 0, -1), id("qtcraft:stone"));
        ticks(QcHangingManager::supportCheckTicks);
        QVERIFY(!hangings().find(second));
        QCOMPARE(droppedCount(item("qtcraft:item_frame")), 2);
    }

    void paintingsTakeTheLargestMotifThatFits()
    {
        join();
        buildWall(7, 4);
        hold(u"painting"_s, 3);
        const QcHangingEntity *large = useOn(m_feet.offset(0, 1, -2), QcBlockFace::South);
        QVERIFY(large);
        QCOMPARE(large->hanging.type, QcHangingType::Painting);
        QCOMPARE(large->hanging.width, 4);
        QCOMPARE(large->hanging.height, 4);
        const QList<QcBlockPos> cells = large->hanging.cells();
        QCOMPARE(cells.size(), 16);
        for (const QcBlockPos cell : cells) {
            QCOMPARE(cell.z, m_feet.z - 1);
            QVERIFY(cell.x >= m_feet.x - 3 && cell.x <= m_feet.x + 3 && cell.y >= m_feet.y && cell.y < m_feet.y + 4);
        }
        const quint8 motif = large->hanging.motif;
        const QcHangingDefinition &definition = *m_content->item(item("qtcraft:painting")).hangs;
        QCOMPARE(definition.motifs[motif].width * definition.motifs[motif].height, 16);

        // The two columns left free at the edge take a motif of 2 x 2; floors take none.
        const QcHangingEntity *narrow = useOn(m_feet.offset(-3, 1, -2), QcBlockFace::South);
        QVERIFY(narrow);
        QCOMPARE(narrow->hanging.width, 2);
        QCOMPARE(narrow->hanging.height, 2);
        QVERIFY(!useOn(m_feet.offset(2, -1, 0), QcBlockFace::Up));

        // A painting falls off when a block of its wall goes.
        const quint32 id = hangings().all().constFirst().id;
        setBlock(m_feet.offset(0, 3, -2), u"air"_s);
        ticks(1);
        QVERIFY(!hangings().find(id));
        QCOMPARE(droppedCount(item("qtcraft:painting")), 1);
    }

    void aComparatorReadsTheTurnOfAFrameBehindItsBlock()
    {
        buildArea();
        // Frame cell (2, 0, 1) on the north face of the wall block (2, 0, 2); the comparator behind
        // the block points south.
        put(2, 0, 2, u"stone"_s);
        put(2, 0, 3, u"signal_comparator[facing=south]"_s);
        ticks(4);
        QCOMPARE(power(2, 0, 3), 0);

        QcHangingEntity hanging = frame(at(2, 0, 1), QcBlockFace::North, "qtcraft:apple", 2);
        hanging.id = m_server->entities().newId();
        QVERIFY(hangings().add(hanging));
        m_server->noteContents(hanging.hanging.cell);
        ticks(4);
        QCOMPARE(power(2, 0, 3), 3);

        hangings().find(hanging.id)->hanging.rotation = 6;
        m_server->noteContents(hanging.hanging.cell);
        ticks(4);
        QCOMPARE(power(2, 0, 3), 7);

        // An empty frame reads 0.
        hangings().find(hanging.id)->held = {};
        m_server->noteContents(hanging.hanging.cell);
        ticks(4);
        QCOMPARE(power(2, 0, 3), 0);

        // Only a frame on the far side of the block counts, not one in the same cell on another face.
        QVERIFY(hangings().remove(hanging.id));
        put(1, 0, 1, u"stone"_s);
        QcHangingEntity side = frame(at(2, 0, 1), QcBlockFace::East, "qtcraft:apple", 4);
        side.id = m_server->entities().newId();
        QVERIFY(hangings().add(side));
        m_server->noteContents(side.hanging.cell);
        ticks(4);
        QCOMPARE(power(2, 0, 3), 0);

        // A frame right behind the comparator without a block between reads nothing.
        QVERIFY(hangings().remove(side.id));
        put(2, 0, 2, u"air"_s);
        put(2, 0, 1, u"stone"_s);
        QcHangingEntity near = frame(at(2, 0, 2), QcBlockFace::South, "qtcraft:apple", 1);
        near.id = m_server->entities().newId();
        QVERIFY(hangings().add(near));
        m_server->noteContents(near.hanging.cell);
        ticks(4);
        QCOMPARE(power(2, 0, 3), 0);
    }

    void framesAndPaintingsAreSavedWithTheirChunk()
    {
        join();
        const QcBlockPos cell{3, 70, -5};
        QcEntityRecord record;
        record.chunk = cell.chunk();
        record.hangings.append({frame(cell, QcBlockFace::East, "qtcraft:apple", 5), 3});
        QcHangingEntity painting;
        painting.item = u"qtcraft:painting"_s;
        painting.hanging.type = QcHangingType::Painting;
        painting.hanging.face = QcBlockFace::West;
        painting.hanging.cell = cell.offset(0, 0, 2);
        painting.hanging.motif = 9;
        painting.hanging.width = 2;
        painting.hanging.height = 2;
        painting.motion.position = painting.hanging.centre();
        record.hangings.append({painting, 4});

        QcLoadWarnings warnings;
        const QcResult<QcEntityRecord> read = QcEntityCodec::decode(QcEntityCodec::encode(record, *m_content), record.chunk, *m_content, warnings);
        QVERIFY(read);
        QVERIFY2(warnings.texts().isEmpty(), qPrintable(warnings.texts().join(u'\n')));
        QCOMPARE(read->hangings.size(), 2);
        QCOMPARE(read->hangings[0].entry, record.hangings[0].entry);
        QCOMPARE(read->hangings[0].order, 3u);
        QCOMPARE(read->hangings[1].entry, painting);

        // A chunk without them writes what earlier versions wrote and reads back empty.
        QcEntityRecord empty;
        empty.chunk = record.chunk;
        empty.populated = true;
        const QByteArray blob = QcEntityCodec::encode(empty, *m_content);
        QCOMPARE(blob.back(), char(1));
        QVERIFY(QcEntityCodec::decode(blob, empty.chunk, *m_content, warnings)->hangings.isEmpty());

        // An entry whose middle lies outside its chunk (a mod made its motif larger) goes with a
        // warning; the chunk and everything else in it stay.
        record.chunk = {record.chunk.x + 1, record.chunk.z};
        record.populated = true;
        QcLoadWarnings moved;
        const QcResult<QcEntityRecord> kept = QcEntityCodec::decode(QcEntityCodec::encode(record, *m_content), record.chunk, *m_content, moved);
        QVERIFY(kept);
        QVERIFY(kept->hangings.isEmpty());
        QVERIFY(kept->populated);
        QCOMPARE(moved.texts().size(), 2);
    }

    void clientsGetWhereTheyHangAndWhatTheyShow()
    {
        QcMsg::EntitySpawn spawn;
        spawn.id = 5;
        spawn.kind = QcEntityKind::Hanging;
        spawn.held = 7;
        spawn.item = {9, 1, 0};
        spawn.hanging = {QcHangingType::Painting, QcBlockFace::South, {4, 64, -2}, 4, 3, 0, 12};
        const QcResult<QcMessage> decoded = QcProtocol::decode(QcProtocol::encode(QcMessage(spawn)), QcProtocol::Peer::Server);
        QVERIFY(decoded && std::holds_alternative<QcMsg::EntitySpawn>(*decoded));
        QCOMPARE(std::get<QcMsg::EntitySpawn>(*decoded).hanging, spawn.hanging);
        QCOMPARE(std::get<QcMsg::EntitySpawn>(*decoded).item, spawn.item);

        // A rotation beyond the eighths is refused.
        spawn.hanging = {QcHangingType::ItemFrame, QcBlockFace::Up, {0, 0, 0}, 1, 1, 8, 0};
        QVERIFY(!QcProtocol::decode(QcProtocol::encode(QcMessage(spawn)), QcProtocol::Peer::Server));
    }

    void aChunkThatCameBackStillChecksItsFrames()
    {
        join();
        buildWall(1, 1);
        hold(u"item_frame"_s);
        const QcHangingEntity placed = *useOn(m_feet.offset(0, 0, -2), QcBlockFace::South);

        // Leaving and coming back with the chunk (decision E-374) keeps the block index right.
        hangings().removeIf([&placed](const QcEntity &entity) { return entity.id == placed.id; });
        QVERIFY(!hangings().find(placed.id));
        QVERIFY(!hangings().at(placed.hanging.cell, placed.hanging.face));
        QCOMPARE(hangings().adopt({placed}), 0);
        QVERIFY(hangings().at(placed.hanging.cell, placed.hanging.face));

        setBlock(m_feet.offset(0, 0, -2), u"air"_s);
        ticks(1);
        QVERIFY(!hangings().find(placed.id));
        QCOMPARE(droppedCount(item("qtcraft:item_frame")), 1);
    }

    void adventureRefusesAndCreativeDropsNothing()
    {
        join();
        buildWall(3, 1);
        hold(u"item_frame"_s, 2);
        const quint32 id = useOn(m_feet.offset(0, 0, -2), QcBlockFace::South)->id;

        chat(u"/gamemode adventure"_s);
        QVERIFY(!useOn(m_feet.offset(1, 0, -2), QcBlockFace::South));
        ticks(cooldownTicks);
        send(QcMsg::AttackEntity{id});
        QVERIFY(hangings().find(id));

        chat(u"/gamemode creative"_s);
        ticks(cooldownTicks);
        send(QcMsg::AttackEntity{id});
        QVERIFY(!hangings().find(id));
        ticks(1);
        QCOMPARE(droppedCount(item("qtcraft:item_frame")), 0);
    }

    void theLimitAndOtherFramesStopPlacing()
    {
        join();
        buildWall(7, 4);
        hold(u"item_frame"_s, 2);
        QVERIFY(useOn(m_feet.offset(0, 1, -2), QcBlockFace::South));

        // A painting is refused over a frame: every motif would cover the clicked cell.
        hold(u"painting"_s);
        QVERIFY(!useOn(m_feet.offset(0, 1, -2), QcBlockFace::South));
        QCOMPARE(countOf(item("qtcraft:painting")), 1);

        // At the limit nothing more hangs, and the item stays in the hand.
        hangings().setLimit(1);
        hold(u"item_frame"_s);
        QVERIFY(!useOn(m_feet.offset(2, 1, -2), QcBlockFace::South));
        QCOMPARE(hangings().all().size(), 1);
        QVERIFY(countOf(item("qtcraft:item_frame")) >= 1);
    }

    void onlyShowcaseStructuresHangThings()
    {
        const auto parse = [](const QByteArray &extra) {
            const QByteArray text = QByteArray("{\"name\": \"a\", \"palette\": {\"#\": \"stone\"}, \"layers\": [[\"#\"]], ") + extra + "}";
            return QcStructureParser::parseStructure(QJsonDocument::fromJson(text).object(), u"qtcraft"_s);
        };
        const QByteArray hanging = "\"hangings\": [{\"at\": [0, 0, 0], \"face\": \"north\", \"item\": \"item_frame\", \"rotation\": 0}]";
        QVERIFY(!parse(hanging));
        const QcResult<QcStructureDefinition> shown = parse(QByteArray("\"showcase\": {\"order\": 1}, ") + hanging);
        QVERIFY2(shown, qPrintable(shown ? QString() : shown.error()));
        QCOMPARE(shown->hangings.size(), 1);
    }

    void motifsNeedTilesOfTheirSize()
    {
        join();
        QcHangingDefinition hangs = *m_content->item(item("qtcraft:painting")).hangs;
        QVERIFY(QcHangingParser::checkTextures(u"painting"_s, hangs, m_content->assets()).isEmpty());
        // A motif made larger than the tiles of its texture is reported.
        const auto large = std::ranges::find_if(hangs.motifs, [](const QcPaintingMotif &motif) { return motif.width == 2 && motif.height == 2; });
        QVERIFY(large != hangs.motifs.end());
        large->width = 4;
        const QStringList problems = QcHangingParser::checkTextures(u"painting"_s, hangs, m_content->assets());
        QCOMPARE(problems.size(), 1);
        QVERIFY2(problems.constFirst().contains(u"\"tiles\": [4, 2]"_s), qPrintable(problems.constFirst()));
    }

    void largeMotifsBringATextureForEveryBlock()
    {
        join();
        const QcHangingDefinition &definition = *m_content->item(item("qtcraft:painting")).hangs;
        QVERIFY(definition.motifs.size() >= 10);
        for (const QcPaintingMotif &motif : definition.motifs) {
            for (int row = 0; row < motif.height; ++row) {
                for (int column = 0; column < motif.width; ++column) {
                    QVERIFY2(m_content->assets().texture(motif.tileTexture(column, row)), qPrintable(motif.tileTexture(column, row)));
                }
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestHanging)
#include "test_hanging.moc"
