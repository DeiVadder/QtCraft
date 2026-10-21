#include "common/maps/QcMapPixel.h"
#include "common/protocol/QcProtocol.h"
#include "QcServerTestFixture.h"
#include "QcTestUtil.h"
#include "server/entities/QcEntities.h"
#include "server/maps/QcMapStore.h"
#include "server/maps/QcServerMaps.h"
#include "worldgen/showcase/QcShowcaseGenerator.h"

#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

// Maps in item frames and the follow-ups of the maps in the hand (decisions E-806 to E-810): who
// receives a framed map, that framed maps stay in memory, the frame markers, the limit of new maps
// in creative mode and the resend of a map taken into the hand.
class TestMapFrames : public QcServerTestFixture
{
    Q_OBJECT

    // Gives the player an empty map and uses it; the filled map's id (0 when that failed). The
    // filled map stays in the hand.
    quint32 makeMap()
    {
        chat(u"/give map 1"_s);
        ticks(1);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:map"))});
        const quint32 next = m_server->maps().nextId();
        send(QcMsg::ConsumeItem{});
        if (m_server->maps().nextId() == next) {
            return 0;
        }
        const QcItemStack made = QcItemStack{item("qtcraft:filled_map"), 1, 0}.withMapId(next);
        for (int slot = 0; slot < m_client->stacks.size(); ++slot) {
            if (m_client->stacks.at(slot).stacksWith(made)) {
                send(QcMsg::SelectHotbarSlot{slot});
            }
        }
        return next;
    }

    // Selects an empty hotbar slot, so no map is held.
    void emptyHand()
    {
        for (int slot = 0; slot < 9; ++slot) {
            if (m_client->stacks.value(slot).isEmpty()) {
                send(QcMsg::SelectHotbarSlot{slot});
                return;
            }
        }
    }

    // Hangs an item frame showing map `map` on the south face of a stone block at `cell` - (0, 0, 1);
    // its entity id.
    quint32 frameWithMap(QcBlockPos cell, quint32 map, int rotation = 0)
    {
        QcHangingEntity hanging;
        hanging.item = u"qtcraft:item_frame"_s;
        hanging.hanging.face = QcBlockFace::South;
        hanging.hanging.cell = cell;
        hanging.hanging.rotation = quint8(rotation);
        hanging.held = QcItemStack{item("qtcraft:filled_map"), 1, 0}.withMapId(map);
        hanging.motion.position = hanging.hanging.centre();
        hanging.id = m_server->entities().newId();
        m_server->world().loadChunk(cell.chunk());
        m_server->world().setBlock(hanging.hanging.supportOf(cell), id("qtcraft:stone"));
        m_server->world().setBlock(cell, qcAirBlockId);
        if (!m_server->entities().hangings().add(hanging)) {
            return 0;
        }
        return hanging.id;
    }

    // The updates of map `map` the client received.
    QList<QcMsg::MapUpdate> updatesOf(quint32 map) const
    {
        QList<QcMsg::MapUpdate> updates;
        for (const QcMsg::MapUpdate &update : std::as_const(m_client->mapUpdates)) {
            if (update.id == map) {
                updates.append(update);
            }
        }
        return updates;
    }

    QRect coveredOf(quint32 map) const
    {
        QRect covered;
        const QList<QcMsg::MapUpdate> updates = updatesOf(map);
        for (const QcMsg::MapUpdate &update : updates) {
            covered = covered.united(update.area);
        }
        return covered;
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void playersNearAFrameReceiveItsMap()
    {
        join();
        const quint32 near = makeMap();
        const quint32 far = makeMap();
        QVERIFY(near > 0);
        QVERIFY(far > near);
        emptyHand();
        QVERIFY(frameWithMap(m_feet.offset(2, 1, -3), near));
        QVERIFY(frameWithMap(m_feet.offset(int(QcServerMaps::frameWatchRange) + 10, 1, 0), far));
        m_client->mapUpdates.clear();
        ticks(QcServerMaps::framedMarkerInterval + 1);

        // The near map goes out whole, the far one not at all.
        QCOMPARE(coveredOf(near), QcMapData::bounds());
        QVERIFY(updatesOf(far).isEmpty());
        QVERIFY(m_server->maps().watches(near, m_client->entityId));
        QVERIFY(!m_server->maps().watches(far, m_client->entityId));

        // A change of the framed map follows, though nobody holds it.
        m_client->mapUpdates.clear();
        QVERIFY(m_server->maps().set(near, {40, 50}, QcMapPixel::make(3, QcMapPixel::Shade::Dark)));
        ticks(1);
        QCOMPARE(coveredOf(near), QRect(40, 50, 1, 1));

        // A framed map shows its frame, not the players near it.
        ticks(QcServerMaps::framedMarkerInterval);
        const QList<QcMsg::MapUpdate> updates = updatesOf(near);
        QVERIFY(!updates.isEmpty());
        const QList<QcMapMarker> markers = updates.constLast().markers;
        QCOMPARE(markers.size(), qsizetype(1));
        const QcMapMarker frame = markers.constFirst();
        QVERIFY(frame.frame);
        QVERIFY(!frame.own);
        QCOMPARE(frame.rotation, quint8(8));

        // Out of range nothing is kept for the player; back in range the map comes whole again.
        m_server->entities().hangings().removeIf([](const QcEntity &) { return true; });
        ticks(1);
        QVERIFY(!m_server->maps().watches(near, m_client->entityId));
        QVERIFY(frameWithMap(m_feet.offset(-2, 1, -3), near));
        m_client->mapUpdates.clear();
        ticks(QcServerMaps::framedMarkerInterval + 1);
        QCOMPARE(coveredOf(near), QcMapData::bounds());
    }

    void framedMapsShareThePixelBudget()
    {
        join();
        QList<quint32> maps;
        for (int index = 0; index < 4; ++index) {
            maps.append(makeMap());
        }
        emptyHand();
        for (int index = 0; index < 4; ++index) {
            QVERIFY(frameWithMap(m_feet.offset(index - 2, 1, -3), maps.at(index)));
        }
        m_client->mapUpdates.clear();
        for (int tick = 0; tick < 3; ++tick) {
            const qsizetype before = m_client->mapUpdates.size();
            ticks(1);
            qsizetype pixels = 0;
            for (qsizetype index = before; index < m_client->mapUpdates.size(); ++index) {
                pixels += m_client->mapUpdates.at(index).pixels.size();
            }
            QVERIFY(pixels <= QcServerMaps::pixelBudget);
        }
        // All four arrive whole within the ticks a whole map each takes.
        ticks(4 * QcMapData::pixelCount / QcServerMaps::pixelBudget);
        for (const quint32 map : std::as_const(maps)) {
            QCOMPARE(coveredOf(map), QcMapData::bounds());
        }
    }

    void heldMapsShowTheirFrames()
    {
        join();
        const quint32 map = makeMap();
        const QcBlockPos cell = m_feet.offset(2, 1, -3);
        QVERIFY(frameWithMap(cell, map));
        m_client->mapUpdates.clear();
        ticks(QcServerMaps::markerInterval + 1);
        const QList<QcMsg::MapUpdate> updates = updatesOf(map);
        QVERIFY(!updates.isEmpty());
        const QList<QcMapMarker> markers = updates.constLast().markers;
        QCOMPARE(markers.size(), qsizetype(2));
        const bool ownShown = std::ranges::any_of(markers, [](QcMapMarker marker) { return marker.own && !marker.frame; });
        QVERIFY(ownShown);

        // The marker stands where the frame hangs and faces away from its wall.
        const QcMapData *data = m_server->maps().find(map);
        QVERIFY(data);
        QcHanging hanging;
        hanging.face = QcBlockFace::South;
        hanging.cell = cell;
        const QcMapMarker expected = QcServerMaps::frameMarker(*data, hanging);
        const bool frameShown = std::ranges::any_of(markers, [&](QcMapMarker marker) { return marker == expected; });
        QVERIFY(frameShown);
        hanging.face = QcBlockFace::East;
        QCOMPARE(QcServerMaps::frameMarker(*data, hanging).rotation, quint8(4));
        // On a floor or ceiling the marker turns a quarter per step, like the picture.
        hanging.face = QcBlockFace::Up;
        hanging.rotation = 3;
        QCOMPARE(QcServerMaps::frameMarker(*data, hanging).rotation, quint8(12));
        hanging.rotation = 5;
        QCOMPARE(QcServerMaps::frameMarker(*data, hanging).rotation, quint8(4));
        hanging.face = QcBlockFace::Down;
        hanging.rotation = 0;
        QCOMPARE(QcServerMaps::frameMarker(*data, hanging).rotation, quint8(8));
        hanging.rotation = 1;
        QCOMPARE(QcServerMaps::frameMarker(*data, hanging).rotation, quint8(4));
    }

    void framesOffTheMapShowNoMarker()
    {
        join();
        const quint32 map = makeMap();
        QVERIFY(frameWithMap(m_feet.offset(2, 1, -3), map));
        // Two maps east: off this map, so no edge marker points towards it.
        QVERIFY(frameWithMap(m_feet.offset(2 * QcMapData::size, 1, -3), map));
        m_client->mapUpdates.clear();
        ticks(QcServerMaps::markerInterval + 1);
        const QList<QcMsg::MapUpdate> updates = updatesOf(map);
        QVERIFY(!updates.isEmpty());
        const QList<QcMapMarker> markers = updates.constLast().markers;
        const qsizetype frames = std::ranges::count_if(markers, [](QcMapMarker marker) { return marker.frame; });
        QCOMPARE(frames, qsizetype(1));
        const bool allOnMap = std::ranges::all_of(markers, [](QcMapMarker marker) { return marker.onMap; });
        QVERIFY(allOnMap);
    }

    void watchingStopsOnlyBeyondTheSlack()
    {
        join();
        emptyHand();
        const std::optional<quint32> map = m_server->maps().create(QcMapData::around(m_feet.x, m_feet.z, 0));
        QVERIFY(map);
        const int range = int(QcServerMaps::frameWatchRange);
        const int slack = int(QcServerMaps::watchSlack);
        const auto hangAt = [&](int east) {
            m_server->entities().hangings().removeIf([](const QcEntity &) { return true; });
            QVERIFY(frameWithMap(m_feet.offset(east, 1, 0), *map));
            ticks(1);
        };

        hangAt(range - slack / 2);
        QVERIFY(m_server->maps().watches(*map, m_client->entityId));
        // Just beyond the range a watched map stays.
        hangAt(range + slack / 2);
        QVERIFY(m_server->maps().watches(*map, m_client->entityId));
        hangAt(range + slack + 2);
        QVERIFY(!m_server->maps().watches(*map, m_client->entityId));
        // A map not watched starts only within the range.
        hangAt(range + slack / 2);
        QVERIFY(!m_server->maps().watches(*map, m_client->entityId));
    }

    void thePlayerWatchesTheNearestMapsUpToTheCap()
    {
        join();
        emptyHand();
        // Frames east and west of the player, two at each distance up to 33, then 34 and 35.
        QHash<int, quint32> mapAt;
        QList<int> offsets;
        for (int east = 2; east <= 35; ++east) {
            offsets.append(east);
            if (east <= 33) {
                offsets.append(-east);
            }
        }
        QCOMPARE(offsets.size(), qsizetype(QcServerMaps::maxWatched + 2));
        for (const int east : std::as_const(offsets)) {
            const std::optional<quint32> map = m_server->maps().create(QcMapData::around(m_feet.x, m_feet.z, 0));
            QVERIFY(map);
            mapAt.insert(east, *map);
            QVERIFY(frameWithMap(m_feet.offset(east, 1, -3), *map));
        }
        ticks(1);
        QCOMPARE(m_server->maps().watched(m_client->entityId).size(), qsizetype(QcServerMaps::maxWatched));
        QVERIFY(m_server->maps().watches(mapAt.value(-33), m_client->entityId));
        QVERIFY(!m_server->maps().watches(mapAt.value(34), m_client->entityId));
        QVERIFY(!m_server->maps().watches(mapAt.value(35), m_client->entityId));

        // A new map only a little nearer than the farthest watched one does not take its place.
        const std::optional<quint32> newcomer = m_server->maps().create(QcMapData::around(m_feet.x, m_feet.z, 0));
        QVERIFY(newcomer);
        QVERIFY(frameWithMap(m_feet.offset(-30, 1, -6), *newcomer));
        ticks(1);
        QVERIFY(!m_server->maps().watches(*newcomer, m_client->entityId));
        QVERIFY(m_server->maps().watches(mapAt.value(-33), m_client->entityId));
    }

    void nearerFramesGetTheirPixelsFirst()
    {
        join();
        emptyHand();
        QList<quint32> maps;
        for (int index = 0; index < 3; ++index) {
            const std::optional<quint32> map = m_server->maps().create(QcMapData::around(m_feet.x, m_feet.z, 0));
            QVERIFY(map);
            maps.append(*map);
        }
        // The first map hangs farthest, the last nearest.
        QVERIFY(frameWithMap(m_feet.offset(20, 1, -3), maps.at(0)));
        QVERIFY(frameWithMap(m_feet.offset(12, 1, -3), maps.at(1)));
        QVERIFY(frameWithMap(m_feet.offset(5, 1, -3), maps.at(2)));
        m_client->mapUpdates.clear();
        ticks(2 * QcMapData::pixelCount / QcServerMaps::pixelBudget);
        QCOMPARE(coveredOf(maps.at(2)), QcMapData::bounds());
        QCOMPARE(coveredOf(maps.at(1)), QcMapData::bounds());
        QVERIFY(coveredOf(maps.at(0)).isEmpty());
    }

    void framedMapsStayInMemory()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        join(config);
        const quint32 framed = makeMap();
        const quint32 loose = makeMap();
        emptyHand();
        // Far from the player: only the frame keeps the map in use.
        QVERIFY(frameWithMap(m_feet.offset(int(QcServerMaps::frameWatchRange) + 10, 1, 0), framed));
        QVERIFY(m_server->save().has_value());
        ticks(int(QcMapStore::idleTicks + QcMapStore::evictionInterval));
        QVERIFY(m_server->maps().loaded(framed));
        QVERIFY(!m_server->maps().loaded(loose));
    }

    void onlyCreativePlayersAreLimitedEvenAfterReconnecting()
    {
        join();
        // The player made its maps of this minute already (counted by name, decision E-810).
        for (int made = 0; made < QcServerMaps::maxMapsPerMinute; ++made) {
            QVERIFY(m_server->maps().countMade(u"Raw"_s, 0, QcServerMaps::maxMapsPerMinute));
        }
        QVERIFY(!m_server->maps().countMade(u"Raw"_s, 0, QcServerMaps::maxMapsPerMinute));
        QVERIFY(m_server->maps().countMade(u"Other"_s, 0, QcServerMaps::maxMapsPerMinute));

        // Survival: every map costs paper, there is no limit.
        const quint32 first = m_server->maps().nextId();
        QVERIFY(makeMap() > 0);
        QCOMPARE(m_server->maps().nextId(), first + 1);

        // Creative, also after reconnecting: no new map within the minute.
        m_client.reset();
        QcTestUtil::pump();
        ticks(2);
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd), m_origin);
        m_client = std::make_unique<RawClient>(std::move(clientEnd));
        m_client->hello(u"Raw"_s);
        QcTestUtil::pump();
        ticks(2);
        chat(u"/gamemode creative"_s);
        chat(u"/give map 1"_s);
        ticks(1);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:map"))});
        send(QcMsg::ConsumeItem{});
        QCOMPARE(m_server->maps().nextId(), first + 1);

        // A minute later it may make maps again.
        ticks(60 * 20);
        send(QcMsg::ConsumeItem{});
        QCOMPARE(m_server->maps().nextId(), first + 2);
    }

    void aMapTakenIntoTheHandComesWhole()
    {
        join();
        const quint32 map = makeMap();
        const int slot = m_client->selected;
        ticks(QcMapData::pixelCount / QcServerMaps::pixelBudget + 2);
        QCOMPARE(coveredOf(map), QcMapData::bounds());

        // While held, only changes follow.
        m_client->mapUpdates.clear();
        ticks(QcServerMaps::markerInterval);
        QVERIFY(coveredOf(map).width() < QcMapData::size || coveredOf(map).height() < QcMapData::size);

        // Put away and taken again: the whole map comes once more.
        emptyHand();
        ticks(1);
        QVERIFY(!m_server->maps().watches(map, m_client->entityId));
        m_client->mapUpdates.clear();
        send(QcMsg::SelectHotbarSlot{slot});
        ticks(QcMapData::pixelCount / QcServerMaps::pixelBudget + 1);
        QCOMPARE(coveredOf(map), QcMapData::bounds());
    }

    void theShowcaseWallMapIsRevealed()
    {
        QElapsedTimer timer;
        timer.start();
        m_server = std::make_unique<QcServer>(QcServerConfig{}, m_content, std::make_unique<QcShowcaseGenerator>(m_content));
        QVERIFY(m_server->openWorld().has_value());
        qInfo() << "showcase world set up in" << timer.elapsed() << "ms";

        // The station's four frames show four maps side by side, north-west first (decision E-811).
        QList<QPoint> centres;
        QList<QcBlockPos> cells;
        for (const QcHangingEntity &hanging : m_server->entities().hangings().all()) {
            if (hanging.held.item != item("qtcraft:filled_map")) {
                continue;
            }
            const QcMapData *map = m_server->maps().find(hanging.held.mapId());
            QVERIFY(map);
            centres.append(map->centre());
            cells.append(hanging.hanging.cell);

            // Revealed over its whole area, not only where chunks happened to be loaded.
            int shown = 0;
            for (int x = 0; x < QcMapData::size; ++x) {
                for (int z = 0; z < QcMapData::size; ++z) {
                    shown += map->at({x, z}) != QcMapPixel::blank ? 1 : 0;
                }
            }
            QVERIFY2(shown == QcMapData::pixelCount, qPrintable(QString::number(shown)));
        }
        QCOMPARE(centres.size(), qsizetype(4));
        const QPoint northWest = *std::ranges::min_element(centres, {}, [](QPoint centre) { return centre.x() + centre.y(); });
        const QList<QPoint> expected{northWest, northWest + QPoint(QcMapData::size, 0), northWest + QPoint(0, QcMapData::size),
                                     northWest + QPoint(QcMapData::size, QcMapData::size)};
        for (const QPoint &centre : expected) {
            QVERIFY(centres.contains(centre));
        }
        // The north-west map lies around the first frame.
        const bool aroundAFrame = std::ranges::any_of(cells, [&](QcBlockPos cell) { return QcMapData::around(cell.x, cell.z, 0).centre() == northWest; });
        QVERIFY(aroundAFrame);
    }

    void frameMarkersTravel()
    {
        QcMsg::MapUpdate update{7, 0, {0, 0}, {}, {}, {}};
        QcMapMarker frame{-5, 9, 12, true, false};
        frame.frame = true;
        update.markers = {frame, QcMapMarker{1, 2, 3, false, true}};
        const QcResult<QcMessage> decoded = QcProtocol::decode(QcProtocol::encode(QcMessage(update)), QcProtocol::Peer::Server);
        QVERIFY(decoded);
        const QList<QcMapMarker> markers = std::get<QcMsg::MapUpdate>(*decoded).markers;
        QCOMPARE(markers, update.markers);
    }
};

QTEST_GUILESS_MAIN(TestMapFrames)
#include "test_map_frames.moc"
