// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/maps/QcMapCrafting.h"
#include "common/maps/QcMapPixel.h"
#include "common/protocol/QcProtocol.h"
#include "QcServerTestFixture.h"
#include "QcTestUtil.h"
#include "server/maps/QcMapSaveCodec.h"
#include "server/maps/QcMapStore.h"
#include "server/maps/QcMapSurvey.h"
#include "server/maps/QcServerMaps.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

// Maps in the server (decisions E-798 to E-804): making, revealing, shading, sending, crafting,
// saving, and what the server and the client refuse.
class TestMaps : public QcServerTestFixture
{
    Q_OBJECT

    // Gives the player an empty map and uses it; the filled map's id (0 when that failed).
    quint32 makeMap()
    {
        chat(u"/give map 1"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:map"))});
        send(QcMsg::ConsumeItem{});
        const int slot = slotOf(item("qtcraft:filled_map"));
        if (slot < 0) {
            return 0;
        }
        send(QcMsg::SelectHotbarSlot{slot});
        const QcItemStack stack = m_client->stacks.value(slot);
        return stack.mapId();
    }

    // Places a crafting table beside the player and opens it.
    void openTable()
    {
        chat(u"/give crafting_table 1"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:crafting_table"))});
        const QcBlockPos table = m_feet.offset(2, 0, 0);
        send(QcMsg::UseItem{table.offset(0, -1, 0), QcBlockFace::Up});
        send(QcMsg::UseItem{table, QcBlockFace::Up});
    }

    // The inventory slot holding filled map `map`, -1 for none.
    int slotOfMap(quint32 map) const
    {
        const QcItemStack wanted = QcItemStack{item("qtcraft:filled_map"), 1, 0}.withMapId(map);
        for (int slot = 0; slot < m_client->stacks.size(); ++slot) {
            const QcItemStack &stack = m_client->stacks.at(slot);
            if (!stack.isEmpty() && stack.stacksWith(wanted)) {
                return slot;
            }
        }
        return -1;
    }

    // Puts one of map `map` in the middle of the open table's grid and paper around it.
    void layOutZoom(quint32 map)
    {
        chat(u"/give paper 8"_s);
        for (int cell = 0; cell < 9; ++cell) {
            const int slot = cell == 4 ? slotOfMap(map) : slotOf(item("qtcraft:paper"));
            send(QcMsg::MoveInventoryItem{slot, QcServer::craftingGridSlot + cell, 1});
        }
    }

    quint32 typeOf(const char *name) const { return m_content->state(id(name)).type; }

    std::optional<quint32> blockShown(quint32 map, int x, int z)
    {
        const QcMapData *data = m_server->maps().find(map);
        const std::optional<QPoint> pixel = data ? data->pixelAt(x, z) : std::nullopt;
        return pixel ? QcMapPixel::blockOf(data->at(*pixel)) : std::nullopt;
    }

    // A frame of a MapUpdate written field by field, so tests can put wrong values in.
    static QByteArray frame(const std::function<void(QDataStream &)> &fields)
    {
        QByteArray bytes;
        QDataStream out(&bytes, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_8);
        out << static_cast<quint16>(QcMessage(QcMsg::MapUpdate{}).index());
        fields(out);
        return bytes;
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void mapsLieOnTheGridOfTheirScale()
    {
        QCOMPARE(QcMapData::around(0, 0, 0).centre(), QPoint(0, 0));
        QCOMPARE(QcMapData::around(63, -64, 0).centre(), QPoint(0, 0));
        QCOMPARE(QcMapData::around(64, -65, 0).centre(), QPoint(128, -128));
        QCOMPARE(QcMapData::around(0, 0, 1).centre(), QPoint(64, 64));
        QCOMPARE(QcMapData::around(-300, 5000, 4).centre(), QPoint(-1088, 5056));
        const QcMapData map = QcMapData::around(10, 10, 2);
        QCOMPARE(map.blocksPerPixel(), 4);
        QCOMPARE(map.pixelAt(map.centre().x(), map.centre().y()), std::optional<QPoint>(QPoint(64, 64)));
        QCOMPARE(map.pixelAt(map.centre().x() - 256, map.centre().y() + 255), std::optional<QPoint>(QPoint(0, 127)));
        QVERIFY(!map.pixelAt(map.centre().x() + 256, 0));
        QCOMPARE(map.columnOf({1, 2}), map.centre() + QPoint(-256 + 4, -256 + 8));
        QCOMPARE(QcMapPixel::blockOf(QcMapPixel::make(5, QcMapPixel::Shade::Dark)), std::optional<quint32>(5));
        QCOMPARE(QcMapPixel::make(QcMapPixel::maxBlock + 1, QcMapPixel::Shade::Normal), QcMapPixel::blank);
    }

    void usingAnEmptyMapMakesAFilledMap()
    {
        join();
        chat(u"/give map 2"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:map"))});
        send(QcMsg::ConsumeItem{});
        QCOMPARE(countOf(item("qtcraft:map")), 1);
        QCOMPARE(countOf(item("qtcraft:filled_map")), 1);
        const QcItemStack filled = m_client->stacks.value(slotOf(item("qtcraft:filled_map")));
        QCOMPARE(filled.mapId(), 1u);

        const QcMapData *map = m_server->maps().find(1);
        QVERIFY(map);
        QCOMPARE(map->scale(), 0);
        QCOMPARE(map->centre(), QcMapData::around(m_feet.x, m_feet.z, 0).centre());
        QVERIFY(!m_server->maps().find(2));

        // The second one replaces the empty map in the hand.
        send(QcMsg::ConsumeItem{});
        QCOMPARE(countOf(item("qtcraft:map")), 0);
        QCOMPARE(m_client->stacks.value(m_client->selected).mapId(), 2u);
    }

    void holdingAMapRevealsTheLandAround()
    {
        join();
        const quint32 map = makeMap();
        QVERIFY(map > 0);
        ticks(QcMapSurvey::sweep + 1);
        QCOMPARE(blockShown(map, m_feet.x, m_feet.z), std::optional<quint32>(typeOf("qtcraft:grass_block")));

        // Columns of chunks that are not loaded stay blank.
        const QcMapData *data = m_server->maps().find(map);
        QPoint unloaded(-1, -1);
        for (int x = 0; x < QcMapData::size && unloaded.x() < 0; x += 8) {
            for (int z = 0; z < QcMapData::size && unloaded.x() < 0; z += 8) {
                const QPoint column = data->columnOf({x, z});
                if (!m_server->world().chunk(QcBlockPos{column.x(), 0, column.y()}.chunk())) {
                    unloaded = {x, z};
                }
            }
        }
        QVERIFY(unloaded.x() >= 0);
        QCOMPARE(data->at(unloaded), QcMapPixel::blank);

        // A map in the inventory but not in the hand draws nothing.
        send(QcMsg::SelectHotbarSlot{(m_client->selected + 1) % 9});
        m_server->world().setBlock(m_feet.offset(0, -1, 0), id("qtcraft:stone"));
        ticks(QcMapSurvey::sweep + 1);
        QCOMPARE(blockShown(map, m_feet.x, m_feet.z), std::optional<quint32>(typeOf("qtcraft:grass_block")));
    }

    void slopesAndWaterAreShaded()
    {
        join();
        const QcServerWorld &world = m_server->world();
        const QcMapData map = QcMapData::around(m_feet.x, m_feet.z, 0);
        const auto shadeAt = [&](QcBlockPos column) {
            const std::optional<QPoint> pixel = map.pixelAt(column.x, column.z);
            const std::optional<quint16> value = pixel ? QcMapSurvey::pixelOf(world, map, *pixel) : std::nullopt;
            return value ? QcMapPixel::shadeOf(*value) : QcMapPixel::Shade::Normal;
        };

        // Three blocks higher than the stage north of it: brighter; the stage south of a pillar: darker.
        const QcBlockPos pillar = m_feet.offset(1, 0, 1);
        for (int dy = 0; dy < 3; ++dy) {
            m_server->world().setBlock(pillar.offset(0, dy, 0), id("qtcraft:stone"));
        }
        QCOMPARE(shadeAt(pillar), QcMapPixel::Shade::Bright);
        QCOMPARE(shadeAt(pillar.offset(0, 0, 1)), QcMapPixel::Shade::Dark);

        // Shallow water is bright, deep water dark.
        const QcBlockPos pond = m_feet.offset(-2, -1, -2);
        m_server->world().setBlock(pond, id("qtcraft:water"));
        QCOMPARE(shadeAt(pond), QcMapPixel::Shade::Bright);
        for (int dy = 0; dy < 12; ++dy) {
            m_server->world().setBlock(pond.offset(0, -dy, 0), id("qtcraft:water"));
        }
        QCOMPARE(shadeAt(pond), QcMapPixel::Shade::Dark);
        const std::optional<QPoint> pixel = map.pixelAt(pond.x, pond.z);
        QVERIFY(pixel);
        const std::optional<quint16> water = QcMapSurvey::pixelOf(world, map, *pixel);
        QVERIFY(water);
        QCOMPARE(QcMapPixel::blockOf(*water), std::optional<quint32>(typeOf("qtcraft:water")));
    }

    void holdersGetChangesWithinTheBudget()
    {
        join();
        const quint32 map = makeMap();
        ticks(QcMapSurvey::sweep + 4);
        QVERIFY(!m_client->mapUpdates.isEmpty());
        QRect covered;
        for (const QcMsg::MapUpdate &update : std::as_const(m_client->mapUpdates)) {
            QCOMPARE(update.id, map);
            QVERIFY(update.area.width() * update.area.height() <= QcServerMaps::pixelBudget);
            QCOMPARE(update.pixels.size(), qsizetype(update.area.width()) * update.area.height());
            covered = covered.united(update.area);
        }
        QCOMPARE(covered, QcMapData::bounds());

        // The client's copy equals the server's map.
        QcMapData mirror = *QcMapData::fromParts(0, m_client->mapUpdates.constFirst().centre, QList<quint16>(QcMapData::pixelCount, 0));
        for (const QcMsg::MapUpdate &update : std::as_const(m_client->mapUpdates)) {
            QVERIFY(update.area.isEmpty() || mirror.setPixels(update.area, update.pixels));
        }
        QCOMPARE(mirror.pixels(), m_server->maps().find(map)->pixels());

        // The own arrow where the player stands.
        const QList<QcMapMarker> markers = m_client->mapUpdates.constLast().markers;
        QCOMPARE(markers.size(), qsizetype(1));
        const QcMapMarker own = markers.constFirst();
        QVERIFY(own.own);
        QVERIFY(own.onMap);
        const QPointF offset = m_server->maps().find(map)->offsetOf(m_client->motion.position.x, m_client->motion.position.z);
        QCOMPARE(int(own.x), int(std::floor(offset.x())));

        // One changed block sends only its neighbourhood.
        m_client->mapUpdates.clear();
        m_server->world().setBlock(m_feet.offset(0, -1, 0), id("qtcraft:stone"));
        ticks(QcMapSurvey::sweep + 1);
        QRect changed;
        for (const QcMsg::MapUpdate &update : std::as_const(m_client->mapUpdates)) {
            changed = changed.united(update.area);
        }
        QVERIFY(changed.width() * changed.height() <= 4);
        QCOMPARE(blockShown(map, m_feet.x, m_feet.z), std::optional<quint32>(typeOf("qtcraft:stone")));
    }

    void mapsCraftLikeTheReference()
    {
        const QcContentRegistry &content = *m_content;
        const QcItemStack filled = QcItemStack{item("qtcraft:filled_map"), 1, 0}.withMapId(7);
        const QcItemStack empty{item("qtcraft:map"), 1, 0};
        const QcItemStack paper{item("qtcraft:paper"), 1, 0};
        using Kind = QcMapCrafting::Result::Kind;

        const std::optional<QcMapCrafting::Result> copy = QcMapCrafting::result(content, {filled, empty, {}, empty}, 2);
        QVERIFY(copy);
        QCOMPARE(copy->kind, Kind::Copy);
        QCOMPARE(copy->stack, filled.withCount(3));
        QVERIFY(!QcMapCrafting::result(content, {filled, filled, empty, {}}, 2));
        QVERIFY(!QcMapCrafting::result(content, {filled, paper, empty, {}}, 2));
        QVERIFY(!QcMapCrafting::result(content, {filled, {}, {}, {}}, 2));

        const QList<QcItemStack> ring{paper, paper, paper, paper, filled, paper, paper, paper, paper};
        const std::optional<QcMapCrafting::Result> zoom = QcMapCrafting::result(content, ring, 3);
        QVERIFY(zoom);
        QCOMPARE(zoom->kind, Kind::Zoom);
        QCOMPARE(zoom->stack, filled);
        QList<QcItemStack> gap = ring;
        gap[0] = {};
        QVERIFY(!QcMapCrafting::result(content, gap, 3));
    }

    void craftingCopiesAndZoomsOut()
    {
        join();
        const quint32 map = makeMap();
        chat(u"/give map 1"_s);
        send(QcMsg::MoveInventoryItem{slotOf(item("qtcraft:filled_map")), QcServer::craftingGridSlot, 1});
        send(QcMsg::MoveInventoryItem{slotOf(item("qtcraft:map")), QcServer::craftingGridSlot + 1, 1});
        const QcItemStack twoCopies = QcItemStack{item("qtcraft:filled_map"), 2, 0}.withMapId(map);
        QCOMPARE(m_client->craftingResult, twoCopies);
        send(QcMsg::TakeCraftingResult{});
        QCOMPARE(countOf(item("qtcraft:filled_map")), 2);
        QCOMPARE(countOf(item("qtcraft:map")), 0);
        // Copies of one map stack (decision E-798).
        const int copies = slotOfMap(map);
        QVERIFY(copies >= 0);
        const QcItemStack copied = m_client->stacks.value(copies);
        QCOMPARE(copied.count, 2);

        // At a crafting table: one copy in the middle, paper around it.
        chat(u"/give crafting_table 1"_s);
        chat(u"/give paper 8"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:crafting_table"))});
        const QcBlockPos table = m_feet.offset(2, 0, 0);
        send(QcMsg::UseItem{table.offset(0, -1, 0), QcBlockFace::Up});
        send(QcMsg::UseItem{table, QcBlockFace::Up});
        QCOMPARE(m_client->craftingSize, 3);
        for (int cell = 0; cell < 9; ++cell) {
            const QcItemId wanted = cell == 4 ? item("qtcraft:filled_map") : item("qtcraft:paper");
            send(QcMsg::MoveInventoryItem{slotOf(wanted), QcServer::craftingGridSlot + cell, 1});
        }
        send(QcMsg::TakeCraftingResult{});
        QCOMPARE(countOf(item("qtcraft:paper")), 0);
        QCOMPARE(countOf(item("qtcraft:filled_map")), 2);
        const quint32 zoomed = map + 1;
        const QcMapData *larger = m_server->maps().find(zoomed);
        QVERIFY(larger);
        QCOMPARE(larger->scale(), 1);
        const QPoint centre = m_server->maps().find(map)->centre();
        QCOMPARE(larger->centre(), QcMapData::around(centre.x(), centre.y(), 1).centre());
    }

    void zoomStopsAtTheLargestScale()
    {
        join();
        quint32 map = makeMap();
        openTable();
        QCOMPARE(m_client->craftingSize, 3);
        for (int scale = 1; scale <= QcMapData::maxScale; ++scale) {
            layOutZoom(map);
            QCOMPARE(m_client->craftingResult.mapId(), map);
            send(QcMsg::TakeCraftingResult{});
            map = m_server->maps().nextId() - 1;
            const QcMapData *zoomed = m_server->maps().find(map);
            QVERIFY(zoomed);
            QCOMPARE(zoomed->scale(), scale);
        }

        // A map at the largest scale shows no result, and taking it makes no map.
        layOutZoom(map);
        QVERIFY(m_client->craftingResult.isEmpty());
        send(QcMsg::TakeCraftingResult{});
        QCOMPARE(m_server->maps().nextId(), map + 1);
    }

    void largerPixelsShowTheMostCommonBlock()
    {
        join();
        const QcServerWorld &world = m_server->world();
        const QcMapData map = QcMapData::around(m_feet.x, m_feet.z, 1);
        const std::optional<QPoint> pixel = map.pixelAt(m_feet.x, m_feet.z);
        QVERIFY(pixel);
        const QPoint corner = map.columnOf(*pixel);
        const int ground = m_feet.y - 1;
        const auto shown = [&] {
            const std::optional<quint16> value = QcMapSurvey::pixelOf(world, map, *pixel);
            return value ? QcMapPixel::blockOf(*value) : std::nullopt;
        };

        // One of the four columns of stone: still grass; three: stone.
        m_server->world().setBlock({corner.x(), ground, corner.y()}, id("qtcraft:stone"));
        QCOMPARE(shown(), std::optional<quint32>(typeOf("qtcraft:grass_block")));
        m_server->world().setBlock({corner.x() + 1, ground, corner.y()}, id("qtcraft:stone"));
        m_server->world().setBlock({corner.x(), ground, corner.y() + 1}, id("qtcraft:stone"));
        QCOMPARE(shown(), std::optional<quint32>(typeOf("qtcraft:stone")));

        // A survey of a column of pixels, which reuses the pixel north of the next, agrees.
        const std::optional<quint16> alone = QcMapSurvey::pixelOf(world, map, *pixel);
        QVERIFY(alone);
        const QList<QcMapSurvey::Surveyed> surveyed = QcMapSurvey::survey(world, map, {*pixel - QPoint(0, 1), *pixel});
        QCOMPARE(surveyed.size(), 2);
        const QcMapSurvey::Surveyed last = surveyed.last();
        QCOMPARE(last.pixel, *pixel);
        QCOMPARE(last.value, *alone);
    }

    void zoomWithoutRoomMakesNoMap()
    {
        join();
        const quint32 map = makeMap();
        chat(u"/give map 1"_s);
        send(QcMsg::MoveInventoryItem{slotOfMap(map), QcServer::craftingGridSlot, 1});
        send(QcMsg::MoveInventoryItem{slotOf(item("qtcraft:map")), QcServer::craftingGridSlot + 1, 1});
        send(QcMsg::TakeCraftingResult{});
        openTable();
        layOutZoom(map);

        // Every slot full but the one holding the other copy of the map: the zoomed map, with a new
        // id, has no room, and no map is made.
        chat(u"/give stone %1"_s.arg(64 * QcInventory::slotCount));
        QVERIFY(slotOfMap(map) >= 0);
        const qsizetype free = std::ranges::count_if(m_client->stacks, [](const QcItemStack &stack) { return stack.isEmpty(); });
        QCOMPARE(free, 0);
        const quint32 next = m_server->maps().nextId();
        send(QcMsg::TakeCraftingResult{});
        send(QcMsg::TakeCraftingResult{});
        QCOMPARE(m_server->maps().nextId(), next);
        QCOMPARE(m_client->craftingResult.mapId(), map);
    }

    void playersCarryingTheMapAreMarkersUntilTheyLeave()
    {
        QcServerConfig config;
        config.operators = {u"Raw"_s, u"Other"_s};
        join(config);
        const quint32 map = makeMap();

        // A creative player picks the map id from the catalogue (decision E-798) and looks east.
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd), m_origin);
        auto other = std::make_unique<RawClient>(std::move(clientEnd));
        other->hello(u"Other"_s);
        QcTestUtil::pump();
        ticks(2);
        other->connection.send(QcMsg::ChatInput{u"/gamemode creative"_s});
        other->connection.send(QcMsg::CreativeItem{0, QcItemStack{item("qtcraft:filled_map"), 1, 0}.withMapId(map)});
        QcPlayerInput look;
        look.sequence = 1;
        look.yaw = 90.0f;
        other->connection.send(QcMsg::PlayerInput{look});
        QcTestUtil::pump();
        ticks(QcServerMaps::markerInterval + 1);
        QVERIFY(m_server->maps().watches(map, other->entityId));

        m_client->mapUpdates.clear();
        ticks(QcServerMaps::markerInterval);
        QVERIFY(!m_client->mapUpdates.isEmpty());
        const QList<QcMapMarker> markers = m_client->mapUpdates.last().markers;
        QCOMPARE(markers.size(), 2);
        const auto byOther = std::ranges::find_if(markers, [](QcMapMarker marker) { return !marker.own; });
        QVERIFY(byOther != markers.end());
        const QcMapMarker seen = *byOther;
        QCOMPARE(seen.rotation, quint8(4));
        QVERIFY(seen.onMap);

        // Leaving: nothing is kept for the player, and its marker goes.
        const quint32 otherId = other->entityId;
        other.reset();
        QcTestUtil::pump();
        ticks(QcServerMaps::markerInterval + 1);
        QVERIFY(!m_server->maps().watches(map, otherId));
        const QList<QcMapMarker> alone = m_client->mapUpdates.last().markers;
        QCOMPARE(alone.size(), 1);
    }

    void theNextMapIdIsSavedWithTheWorld()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        join(config);
        const quint32 map = makeMap();
        QVERIFY(m_server->save().has_value());
        cleanup();

        // A lost map file does not free its id; a file name beyond the largest id blocks none.
        QVERIFY(QFile::remove(QDir(directory.path()).filePath(QcMapSaveCodec::fileName(map))));
        QFile stray(QDir(directory.path()).filePath(QcMapSaveCodec::fileName(QcMapStore::maxId + 5)));
        QVERIFY(stray.open(QIODevice::WriteOnly));
        stray.close();
        QVERIFY(!QcMapSaveCodec::idOf(stray.fileName()));
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_server->openWorld().has_value());
        QCOMPARE(m_server->maps().nextId(), map + 1);
    }

    void savedMapsComeBackAndOnlyChangedOnesAreWritten()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        join(config);
        const quint32 map = makeMap();
        ticks(QcMapSurvey::sweep + 1);
        QVERIFY(m_server->save().has_value());
        const QString file = QDir(directory.path()).filePath(QcMapSaveCodec::fileName(map));
        QVERIFY(QFile::exists(file));
        const QcMapData kept = *m_server->maps().find(map);
        QVERIFY(kept.revealedCount() > 0);

        // Unchanged since the last save: not written again.
        send(QcMsg::SelectHotbarSlot{(m_client->selected + 1) % 9});
        QVERIFY(QFile::remove(file));
        QVERIFY(m_server->save().has_value());
        QVERIFY(!QFile::exists(file));
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:filled_map"))});
        m_server->world().setBlock(m_feet.offset(0, -1, 0), id("qtcraft:stone"));
        ticks(QcMapSurvey::sweep + 1);
        QVERIFY(m_server->save().has_value());
        QVERIFY(QFile::exists(file));
        const QcMapData changed = *m_server->maps().find(map);

        cleanup();
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_server->openWorld().has_value());
        QCOMPARE(m_server->maps().nextId(), map + 1);
        const QcMapData *read = m_server->maps().find(map);
        QVERIFY(read);
        QVERIFY(*read == changed);
    }

    void damagedMapFilesAreRefused()
    {
        const QcMapData map = *QcMapData::fromParts(2, {512, -512}, QList<quint16>(QcMapData::pixelCount, QcMapPixel::make(typeOf("qtcraft:stone"), QcMapPixel::Shade::Dark)));
        const QByteArray bytes = QcMapSaveCodec::write(map, *m_content);
        const QcResult<QcMapData> back = QcMapSaveCodec::read(bytes, u"map"_s, *m_content);
        QVERIFY(back);
        QVERIFY(*back == map);
        QVERIFY(!QcMapSaveCodec::read(bytes.left(bytes.size() - 3), u"map"_s, *m_content));
        QVERIFY(!QcMapSaveCodec::read(QByteArray(QcMapSaveCodec::maxBytes + 1, 'x'), u"map"_s, *m_content));
        QCOMPARE(QcMapSaveCodec::idOf(u"maps/12.qcmap"_s), std::optional<quint32>(12));
        QVERIFY(!QcMapSaveCodec::idOf(u"maps/0.qcmap"_s));
        QVERIFY(!QcMapSaveCodec::idOf(u"maps/x.qcmap.next"_s));

        // A damaged file in a world: the map is unknown, its id stays used.
        QTemporaryDir directory;
        QVERIFY(QDir(directory.path()).mkpath(u"maps"_s));
        QFile damaged(QDir(directory.path()).filePath(QcMapSaveCodec::fileName(3)));
        QVERIFY(damaged.open(QIODevice::WriteOnly));
        damaged.write("QCMP garbage");
        damaged.close();
        QcServerConfig config;
        config.worldDirectory = directory.path();
        join(config);
        QVERIFY(!m_server->maps().find(3));
        QCOMPARE(m_server->maps().nextId(), quint32(4));
    }

    void clientsRefuseBadMapUpdates()
    {
        const auto decoded = [](const QByteArray &bytes) { return QcProtocol::decode(bytes, QcProtocol::Peer::Server).has_value(); };
        const auto header = [](QDataStream &out, quint8 width, quint8 height) {
            out << quint32{1} << quint8{0} << qint32{0} << qint32{0} << quint8{0} << quint8{0} << width << height;
        };
        // A valid update for comparison.
        QVERIFY(decoded(frame([&](QDataStream &out) {
            header(out, 1, 1);
            out << false << QByteArray(2, '\0') << quint8{0};
        })));
        // A rectangle beyond the map's edge.
        QVERIFY(!decoded(frame([](QDataStream &out) {
            out << quint32{1} << quint8{0} << qint32{0} << qint32{0} << quint8{127} << quint8{0} << quint8{2} << quint8{1};
            out << false << QByteArray(4, '\0') << quint8{0};
        })));
        // Too few pixels for the rectangle.
        QVERIFY(!decoded(frame([&](QDataStream &out) {
            header(out, 2, 2);
            out << false << QByteArray(6, '\0') << quint8{0};
        })));
        // A compressed block claiming more than the rectangle.
        QVERIFY(!decoded(frame([&](QDataStream &out) {
            header(out, 1, 1);
            out << true << qCompress(QByteArray(1 << 20, '\0')) << quint8(0);
        })));
        // Too many markers, a turn beyond the sixteenths, a scale beyond the largest, id 0.
        QVERIFY(!decoded(frame([&](QDataStream &out) {
            header(out, 0, 0);
            out << static_cast<quint8>(QcMapMarker::maxCount + 1);
        })));
        QVERIFY(!decoded(frame([&](QDataStream &out) {
            header(out, 0, 0);
            out << quint8{1} << qint8{0} << qint8{0} << quint8{16} << quint8{1};
        })));
        QVERIFY(!decoded(frame([](QDataStream &out) {
            out << quint32{1} << static_cast<quint8>(QcMapData::maxScale + 1) << qint32{0} << qint32{0}
                << quint8{0} << quint8{0} << quint8{0} << quint8{0} << quint8{0};
        })));
        QVERIFY(!decoded(frame([](QDataStream &out) {
            out << quint32{0} << quint8{0} << qint32{0} << qint32{0} << quint8{0} << quint8{0} << quint8{0} << quint8{0} << quint8{0};
        })));

        // A whole map compresses well and comes back equal.
        QcMsg::MapUpdate whole{5, 1, {64, 64}, QcMapData::bounds(), QList<quint16>(QcMapData::pixelCount, 9), {{1, -2, 3, true, true}}};
        const QByteArray bytes = QcProtocol::encode(whole);
        QVERIFY(bytes.size() < 2048);
        const QcResult<QcMessage> back = QcProtocol::decode(bytes, QcProtocol::Peer::Server);
        QVERIFY(back);
        const auto *update = std::get_if<QcMsg::MapUpdate>(&*back);
        QVERIFY(update);
        QCOMPARE(update->pixels, whole.pixels);
        QCOMPARE(update->markers, whole.markers);
    }
};

QTEST_GUILESS_MAIN(TestMaps)
#include "test_maps.moc"
