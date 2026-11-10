// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/blockrules/QcSafeStand.h"
#include "common/world/QcBlockAccess.h"
#include "fixtures/QcTestUtil.h"

#include <QHash>
#include <QSet>
#include <QTest>

using namespace Qt::StringLiterals;

// Where a player may stand (decision E-1111), on a small made-up world: netherrack below y 64, air
// from there up, single blocks set by the test, cells that count as not loaded.
namespace {

constexpr int groundY = 64;
constexpr QcBlockPos feet{0, groundY, 0};

class FakeWorld : public QcBlockAccess
{
public:
    explicit FakeWorld(const QcContentRegistry &content)
        : m_content(content)
    {
    }

    std::optional<QcBlockId> blockAt(QcBlockPos pos) const override
    {
        if (m_unloaded.contains(pos)) {
            return std::nullopt;
        }
        if (const auto it = m_blocks.constFind(pos); it != m_blocks.cend()) {
            return *it;
        }
        return pos.y < groundY ? *m_content.blockId(u"qtcraft:netherrack"_s) : qcAirBlockId;
    }

    const QcContentRegistry &content() const override { return m_content; }

    void set(QcBlockPos pos, const char *name) { m_blocks.insert(pos, *m_content.blockId(QString::fromLatin1(name))); }
    void unload(QcBlockPos pos) { m_unloaded.insert(pos); }

private:
    const QcContentRegistry &m_content;
    QHash<QcBlockPos, QcBlockId> m_blocks;
    QSet<QcBlockPos> m_unloaded;
};

} // namespace

class TestSafeStand : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

private slots:
    void plainGroundFits()
    {
        const FakeWorld world(*m_content);
        QVERIFY(QcSafeStand::fits(world, feet));
    }

    // Lava within hazardRadius rejects, one further accepts.
    void lavaNearbyRejects_data()
    {
        QTest::addColumn<int>("distance");
        QTest::addColumn<bool>("fits");
        QTest::newRow("distance 2") << 2 << false;
        QTest::newRow("distance 3") << 3 << true;
    }
    void lavaNearbyRejects()
    {
        QFETCH(int, distance);
        QFETCH(bool, fits);
        FakeWorld world(*m_content);
        world.set(feet.offset(distance, -1, 0), "qtcraft:lava");
        QCOMPARE(QcSafeStand::fits(world, feet), fits);
    }

    void hurtingGroundRejects_data()
    {
        QTest::addColumn<QString>("block");
        QTest::newRow("magma") << u"qtcraft:magma_block"_s;
        QTest::newRow("lit campfire") << u"qtcraft:campfire"_s;
    }
    void hurtingGroundRejects()
    {
        QFETCH(QString, block);
        FakeWorld world(*m_content);
        world.set(feet.offset(0, -1, 0), block.toLatin1().constData());
        QVERIFY(!QcSafeStand::roomAt(world, feet));
        QVERIFY(!QcSafeStand::fits(world, feet));
    }

    void fireAtTheFeetRejects_data()
    {
        QTest::addColumn<QString>("block");
        QTest::newRow("fire") << u"qtcraft:fire"_s;
        QTest::newRow("soul fire") << u"qtcraft:soul_fire"_s;
    }
    void fireAtTheFeetRejects()
    {
        QFETCH(QString, block);
        FakeWorld world(*m_content);
        world.set(feet, block.toLatin1().constData());
        QVERIFY(!QcSafeStand::roomAt(world, feet));
    }

    void cobwebAtTheHeadRejects()
    {
        FakeWorld world(*m_content);
        world.set(feet.offset(0, 1, 0), "qtcraft:cobweb");
        QVERIFY(!QcSafeStand::roomAt(world, feet));
    }

    void anUnloadedCellRejects()
    {
        FakeWorld world(*m_content);
        world.unload(feet.offset(2, 1, -2));
        QVERIFY(!QcSafeStand::fits(world, feet));
    }
};

QTEST_GUILESS_MAIN(TestSafeStand)
#include "test_safe_stand.moc"
