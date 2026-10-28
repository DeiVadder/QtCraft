#include "QcBlockEntityDump.h"
#include "QcMachineFixture.h"
#include "QcTestUtil.h"
#include "server/entities/QcEntityChecks.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <cmath>

using namespace Qt::StringLiterals;

// Comparison runs of the entities (stage 31.2): creatures, dropped items, a falling block, an armour
// stand, a sliding block and the player in a scripted scenario tick by tick, pistons pushing every
// kind of body in four rotations, and a world saved with entities in motion and opened again. Each
// tick gives the entities as the server keeps them and every spawn, move and removal the client got.
// The dump must equal the one the code before the entity model wrote
// (tests/data/entity-compare-before-31-2.txt, from this test in its first version), plus the one
// move an item or cart sends in the tick it comes to rest (its stop sample, decision E-399), with the
// creatures walking their paths and the zombie no longer striking the player through the floor
// (path finding and melee with a line of sight, decision E-400) and hunters that pick only players
// they see and wander until then (decision E-401), and with pigs instead of sheep as the animals a
// chunk brings since chickens joined the peaceful kinds (decision E-452). With
// QTCRAFT_COMPARE_DUMP=<file> the dump is also written there; QTCRAFT_COMPARE_WORLD=<dir> keeps a
// copy of the saved world.
class TestEntityCompare : public QcMachineFixture
{
    Q_OBJECT

public:
    // Chunks load at once, so the run does not depend on how fast the threads are (stage 31.3).
    TestEntityCompare() { m_loadChunksAtOnce = true; }

private:
    QStringList m_dump;
    // Runs that add to the dump; it is compared only when all of them ran.
    int m_dumpRuns = 0;
    static constexpr int allDumpRuns = 6;
    // Ids of the entities the test makes itself, far from the server's own.
    quint32 m_nextId = 2'000'000;
    // What the client heard since the last record.
    QStringList m_heard;

    static QString num(double value) { return QString::number(value, 'f', 6); }
    static QString vec(const QcVec3 &v) { return num(v.x) + u' ' + num(v.y) + u' ' + num(v.z); }
    QString stackText(const QcItemStack &stack) const { return QcBlockEntityDump::stackText(stack, *m_content); }

    QString equipmentText(const QcEquipment &equipment) const
    {
        QStringList stacks;
        for (const QcItemStack &stack : equipment.stacks) {
            stacks.append(stackText(stack));
        }
        return stacks.join(u',');
    }

    static QString poseText(const QcStandPose &pose)
    {
        QStringList parts;
        for (const QcStandPose::Rotation &rotation : pose.parts) {
            parts.append(u"%1/%2/%3"_s.arg(rotation.x).arg(rotation.y).arg(rotation.z));
        }
        return u"%1 %2"_s.arg(pose.flags()).arg(parts.join(u','));
    }

    // The kind of an entity as the client is told, in the words this dump has always used.
    QString kindOf(const QcMsg::EntitySpawn &spawn) const
    {
        switch (spawn.kind) {
        case QcEntityKind::Player:
            return u"player"_s;
        case QcEntityKind::Item:
            return u"item"_s;
        case QcEntityKind::FallingBlock:
            return u"falling"_s;
        case QcEntityKind::ArmourStand:
            return u"stand"_s;
        case QcEntityKind::MovingBlock:
            return u"moving"_s;
        case QcEntityKind::Minecart:
            return u"minecart"_s;
        case QcEntityKind::ExperienceOrb:
            return u"orb"_s;
        case QcEntityKind::Projectile:
            return u"projectile"_s;
        case QcEntityKind::PrimedExplosive:
            return u"primed"_s;
        case QcEntityKind::Hanging:
            return u"hanging"_s;
        case QcEntityKind::Boat:
            return u"boat"_s;
        case QcEntityKind::Creature:
            break;
        }
        return u"creature:"_s + m_content->mobs().value(spawn.creatureType).name;
    }

    QString spawnText(const QcMsg::EntitySpawn &spawn) const
    {
        return u"spawn %1 %2 '%3' at %4 yaw %5 health %6 item %7 pitch %8 flags %9 held %10 block %11 equipment %12 stand %13 cape '%14'"_s
            .arg(spawn.id)
            .arg(kindOf(spawn), spawn.name, vec(spawn.position), num(spawn.yaw), num(spawn.health), stackText(spawn.item), num(spawn.pitch))
            .arg(QString::number(spawn.flags), spawn.held == qcNoItem ? u"-"_s : m_content->item(spawn.held).name,
                 m_content->stateName(spawn.block), equipmentText(spawn.equipment), poseText(spawn.stand), spawn.appearance.cape);
    }

    void heard(const QcMessage &message)
    {
        if (const auto *spawn = std::get_if<QcMsg::EntitySpawn>(&message)) {
            m_heard.append(spawnText(*spawn));
        } else if (const auto *moves = std::get_if<QcMsg::EntityMoves>(&message)) {
            m_heard.append(u"moves at %1 ms"_s.arg(moves->timeMs));
            for (const QcMsg::EntityMove &move : moves->moves) {
                m_heard.append(u"move %1 at %2 yaw %3 health %4 hurt %5 pitch %6 flags %7 held %8"_s.arg(move.id)
                                   .arg(vec(move.position), num(move.yaw), num(move.health))
                                   .arg(move.hurt)
                                   .arg(num(move.pitch))
                                   .arg(move.flags)
                                   .arg(move.held == qcNoItem ? u"-"_s : m_content->item(move.held).name));
            }
        } else if (const auto *removed = std::get_if<QcMsg::EntityRemove>(&message)) {
            m_heard.append(u"remove %1 died %2"_s.arg(removed->id).arg(removed->died));
        }
    }

    // A position in the frame of the area, turned back (the same in every rotation).
    QcVec3 local(const QcVec3 &world) const
    {
        double x = world.x - m_origin.x;
        double z = world.z - m_origin.z;
        for (int i = 0; i < ((m_turns % 4) + 4) % 4; ++i) {
            const double turnedX = z;
            z = areaSize - x;
            x = turnedX;
        }
        return {x, world.y - m_origin.y, z};
    }

    QString where(const QcVec3 &position) const { return vec(local(position)); }

    // The entities as the server keeps them, in the order it keeps them.
    QStringList serverLines() const
    {
        QStringList lines;
        for (const QcCreature &mob : m_server->mobs().mobs()) {
            lines.append(u"creature %1 %2 at %3 velocity %4 yaw %5 health %6 dormant %7 young %8"_s.arg(mob.id)
                             .arg(m_content->mobs()[mob.type].name, where(mob.motion.position), vec(mob.motion.velocity), num(mob.yaw), num(mob.health))
                             .arg(mob.dormant)
                             .arg(mob.isYoung()));
        }
        for (const QcItemEntity &item : m_server->items().items()) {
            lines.append(u"item %1 %2 at %3 velocity %4 age %5 delay %6 resting %7 asleep %8"_s.arg(item.id)
                             .arg(stackText(item.stack), where(item.motion.position), vec(item.motion.velocity), num(item.age), num(item.pickupDelay))
                             .arg(item.resting)
                             .arg(item.surplus));
        }
        lines.append(u"queued items %1"_s.arg(m_server->items().queued().size()));
        for (const QcFallingBlock &falling : m_server->fallingBlocks().all()) {
            lines.append(u"falling %1 %2 at %3 velocity %4 age %5"_s.arg(falling.id)
                             .arg(m_content->stateName(falling.block), where(falling.motion.position), vec(falling.motion.velocity), num(falling.age)));
        }
        for (const QcArmourStand &stand : m_server->stands().stands()) {
            lines.append(u"stand %1 at %2 yaw %3 name '%4' held %5 equipment %6 pose %7"_s.arg(stand.id)
                             .arg(where(stand.motion.position), num(stand.yaw), stand.name, stackText(stand.held), equipmentText(stand.equipment),
                                  poseText(stand.pose)));
        }
        for (const QcMovingBlock &moving : m_server->movingBlocks().all()) {
            lines.append(u"moving %1 %2 from %3 to %4 start %5"_s.arg(moving.id)
                             .arg(m_content->stateName(moving.block), QcBlockEntityDump::posText(moving.from), QcBlockEntityDump::posText(moving.to))
                             .arg(moving.start));
        }
        lines.append(u"player at %1 health %2"_s.arg(where(m_client->motion.position), num(m_client->health)));
        return lines;
    }

    void record(const QString &title)
    {
        const QStringList problems = QcEntityChecks::problems(m_server->entities());
        QVERIFY2(problems.isEmpty(), qPrintable(title + u": "_s + problems.join(u"; "_s)));
        m_dump.append(u"== "_s + title);
        m_dump.append(serverLines());
        m_dump.append(m_heard);
        m_heard.clear();
    }

    void listen()
    {
        m_heard.clear();
        m_client->onEntityMessage = [this](const QcMessage &message) { heard(message); };
    }

    int mobType(const char *name) const { return *m_content->mobIndex(QString::fromLatin1(name)); }

    // The middle of the floor of local cell (x, y, z).
    QcVec3 feetAt(int x, int y, int z) const
    {
        const QcBlockPos cell = at(x, y, z);
        return {cell.x + 0.5, static_cast<double>(cell.y), cell.z + 0.5};
    }

    quint32 creature(const char *name, int x, int y, int z, bool persistent = true)
    {
        const quint32 id = m_nextId++;
        m_server->mobs().spawn(mobType(name), feetAt(x, y, z), id, false, persistent);
        return id;
    }

    quint32 stand(int x, int y, int z)
    {
        QcArmourStand stand;
        stand.id = m_nextId++;
        stand.motion.position = feetAt(x, y, z);
        stand.yaw = 90.0f;
        stand.name = u"Stand"_s;
        stand.item = u"qtcraft:armor_stand"_s;
        stand.held = stack("qtcraft:stick", 1);
        stand.equipment.at(QcEquipmentSlot::Chest) = stack("qtcraft:iron_chestplate", 1);
        stand.pose.showName = true;
        if (!m_server->stands().add(stand)) {
            QcTestUtil::failCase(QStringLiteral("no room for a stand"));
            return 0;
        }
        return stand.id;
    }

    quint32 thrown(const QcVec3 &from, const QcVec3 &velocity, const QcItemStack &thrownStack)
    {
        QcItemEntityManager::Drop entity;
        entity.stack = thrownStack;
        entity.position = from;
        entity.velocity = velocity;
        entity.pickupDelay = 0.0;
        return m_server->items().spawn(entity, m_nextId++);
    }

    void setBlockByCommand(int x, int y, int z, const char *block)
    {
        const QcBlockPos pos = at(x, y, z);
        chat(u"/setblock %1 %2 %3 %4"_s.arg(pos.x).arg(pos.y).arg(pos.z).arg(QLatin1StringView(block)));
    }

    void reopen(const QcServerConfig &config)
    {
        QVERIFY(m_server->save().has_value());
        if (const QString keep = qEnvironmentVariable("QTCRAFT_COMPARE_WORLD"); !keep.isEmpty()) {
            copyWorld(config.worldDirectory, keep);
        }
        m_client.reset();
        m_server.reset();
        join(config);
    }

    static void copyWorld(const QString &from, const QString &to)
    {
        QDirIterator files(from, QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const QString path = files.next();
            const QString relative = QDir(from).relativeFilePath(path);
            if (relative == u"session.lock"_s) {
                continue;
            }
            const QString target = QDir(to).filePath(relative);
            QDir().mkpath(QFileInfo(target).path());
            QFile::remove(target);
            QFile::copy(path, target);
        }
    }

    // A lever and a piston facing east (or up) pushing local cell (x + 1, y, z) (or the cell above).
    void pistonLane(int x, int z, bool up = false)
    {
        put(x - 1, 0, z, u"lever[attach=floor]"_s);
        put(x, 0, z, up ? u"signal_piston[facing=up]"_s : u"signal_piston[facing=east]"_s);
    }

private slots:
    // The recorded scenario stands by day; its zombie keeps out of the sun's fire, which came later
    // (decision E-426).
    void initTestCase()
    {
        auto content = std::make_shared<QcContentRegistry>(*m_content);
        QcMobDefinition zombie = content->mobs().at(*content->mobIndex(u"qtcraft:zombie"_s));
        zombie.burnsInDaylight = false;
        content->registerMob(zombie);
        m_content = content;
    }

    void cleanupTestCase()
    {
        if (const QString path = qEnvironmentVariable("QTCRAFT_COMPARE_DUMP"); !path.isEmpty()) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(m_dump.join(u'\n').toUtf8() + '\n');
        }
        if (m_dumpRuns != allDumpRuns) {
            return;
        }
        QFile golden(QStringLiteral(QTCRAFT_COMPARE_GOLDEN));
        QVERIFY(golden.open(QIODevice::ReadOnly));
        const QStringList before = QString::fromUtf8(golden.readAll()).split(u'\n', Qt::SkipEmptyParts);

        // Everything up to the reopened world is compared line by line. A reopened world brings its
        // entities back with new ids as their chunks load (decision E-374): right after reopening the
        // same entities are there (whole, creatures without where they wandered to); the ticks after
        // that run in the new order and are not compared, test_chunk_unloading compares states.
        const qsizetype reopenedNow = m_dump.indexOf(u"== reopened"_s);
        const qsizetype reopenedThen = before.indexOf(u"== reopened"_s);
        QVERIFY(reopenedNow > 0 && reopenedThen > 0);
        QCOMPARE(reopenedNow, reopenedThen);
        for (qsizetype i = 0; i < reopenedNow; ++i) {
            const QString now = m_dump[i];
            const QString &then = before[i];
            QCOMPARE(now, then);
        }
        const QStringList entitiesNow = reopenedBlock(m_dump, reopenedNow);
        const QStringList entitiesThen = reopenedBlock(before, reopenedThen);
        QCOMPARE(entitiesNow, entitiesThen);
    }

    // The entities of the block starting at `start` without their ids, sorted. Items, falling blocks,
    // stands and the player are compared whole; creatures by kind, health and what they are, since
    // where they wandered in the ticks after reopening depends on their new ids.
    static QStringList reopenedBlock(const QStringList &dump, qsizetype start)
    {
        static const QRegularExpression id(uR"(^(creature|item|falling|stand|moving) \d+ )"_s);
        static const QRegularExpression motion(uR"( at \S+ \S+ \S+ velocity \S+ \S+ \S+ yaw \S+)"_s);
        QStringList block;
        for (qsizetype i = start + 1; i < dump.size() && !dump[i].startsWith(u"=="_s); ++i) {
            QString line = dump[i];
            line.replace(id, u"\\1 "_s);
            if (line.startsWith(u"creature"_s)) {
                line.remove(motion);
            }
            block.append(line);
        }
        block.sort();
        return block;
    }

    // Every kind in one scene, tick by tick: creatures wander and chase, items merge, fall through a
    // hole and are picked up, a block falls, a stand stands and a piston slides a block.
    void sceneTickByTick()
    {
        buildArea();
        listen();
        creature("qtcraft:pig", 3, 0, 3);
        creature("qtcraft:zombie", 10, 0, 10, false);
        creature("qtcraft:slime", 6, 0, 11, false);
        drop(2, 0.0, 8, stack("qtcraft:stick", 3));
        drop(2, 0.0, 8, stack("qtcraft:stick", 2));
        drop(12, 0.0, 1, stack("qtcraft:diamond", 1));
        thrown(feetAt(11, 1, 6), {5.0, 4.0, 0.5}, stack("qtcraft:cobblestone", 4));
        // A hole above the player: the item falls to him and is picked up.
        put(7, -1, 7, u"air"_s);
        drop(7, 0.5, 7, stack("qtcraft:coal", 2));
        stand(9, 0, 3);
        setBlockByCommand(5, 5, 5, "qtcraft:sand");
        pistonLane(2, 13);
        put(3, 0, 13, u"stone"_s);
        for (int tick = 0; tick < 100; ++tick) {
            if (tick == 30) {
                flip(1, 0, 13);
            }
            ticks(1);
            record(u"scene tick %1"_s.arg(tick));
        }
        m_client.reset();
        m_server.reset();
        ++m_dumpRuns;
    }

    void pistonsPushEveryKind_data()
    {
        QTest::addColumn<int>("turns");
        for (int turns = 0; turns < 4; ++turns) {
            QTest::addRow("turns %d", turns) << turns;
        }
    }

    // Pistons push a creature, an item, a stand and the player sideways, an item and a creature up.
    void pistonsPushEveryKind()
    {
        QFETCH(int, turns);
        m_turns = turns;
        buildArea();
        listen();
        pistonLane(2, 1);
        creature("qtcraft:pig", 3, 0, 1);
        pistonLane(2, 4);
        drop(3, 0.0, 4, stack("qtcraft:stick", 1));
        pistonLane(2, 7);
        stand(3, 0, 7);
        pistonLane(2, 10);
        const QcVec3 player = feetAt(3, 0, 10);
        chat(u"/tp %1 %2 %3"_s.arg(player.x).arg(player.y).arg(player.z));
        pistonLane(11, 3, true);
        drop(11, 1.0, 3, stack("qtcraft:diamond", 1));
        pistonLane(11, 9, true);
        creature("qtcraft:sheep", 11, 1, 9);
        ticks(3);
        record(u"pistons turns %1 before"_s.arg(turns));
        flip(1, 0, 1);
        flip(1, 0, 4);
        flip(1, 0, 7);
        flip(1, 0, 10);
        flip(10, 0, 3);
        flip(10, 0, 9);
        for (int tick = 0; tick < 4; ++tick) {
            ticks(1);
            record(u"pistons turns %1 tick %2"_s.arg(turns).arg(tick));
        }
        m_client.reset();
        m_server.reset();
        ++m_dumpRuns;
    }

    // A world saved with a block falling, an item flying, creatures and a stand, then opened again.
    void savedInMotionAndReopened()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 0;
        buildArea(config);
        creature("qtcraft:pig", 3, 0, 3);
        creature("qtcraft:sheep", 4, 0, 9);
        creature("qtcraft:zombie", 10, 0, 10, false);
        drop(2, 0.0, 8, stack("qtcraft:stick", 3));
        drop(12, 0.0, 1, stack("qtcraft:diamond", 1));
        thrown(feetAt(6, 2, 6), {1.0, 3.0, -0.5}, stack("qtcraft:cobblestone", 4));
        stand(9, 0, 3);
        setBlockByCommand(5, 7, 5, "qtcraft:gravel");
        ticks(4);
        listen();
        record(u"saved"_s);
        reopen(config);
        listen();
        record(u"reopened"_s);
        for (int tick = 0; tick < 40; ++tick) {
            ticks(1);
            if (tick % 10 == 9) {
                record(u"reopened tick %1"_s.arg(tick));
            }
        }
        m_client.reset();
        m_server.reset();
        ++m_dumpRuns;
    }
};

QTEST_GUILESS_MAIN(TestEntityCompare)
#include "test_entity_compare.moc"
